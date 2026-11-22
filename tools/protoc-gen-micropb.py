#!/usr/bin/env python3
"""protoc plugin: writes C++ structs for MicroPB from a proto3 file.

    protoc --plugin=protoc-gen-micropb=tools/protoc-gen-micropb.py --micropb_out=gen app.proto

The output is gen/app.pb.hpp: plain structs, and a field list for each struct. The sizes of repeated
fields come from app.options, a file in the NanoPB format:

    # <name pattern> <option>:<value> ...
    Message.field  (no option)                    repeated: std::span<const T>; decode needs a pb::arena
    Message.field  max_count:8                    pb::repeated<T, 8>
    Message.field  max_count:3 fixed_count:true   std::array<T, 3>
    Message.field  max_size:16                    bytes: pb::bytes<16>; string: pb::string<15>
    Message.field  max_length:15                  string: pb::string<15>
    Message.field  max_size:6 fixed_length:true   bytes: std::array<uint8_t, 6>
    Message.field  int_size:IS_8                  int8_t or uint8_t (also IS_16, IS_32, IS_64)
    Message.field  type:FT_POINTER                repeated: std::span<const T>, also with max_count;
                                                    bytes: std::span<const uint8_t>; string: std::string_view
    Enum           enum_intsize:IS_8              enum class Enum : int8_t (also IS_16, IS_32)

The plugin looks for app.options with the path that protoc has for app.proto: first from the
current folder, then from each folder of the parameter --micropb_opt=options_dir=<folder>. With
`-I proto proto/app.proto`, give `--micropb_opt=options_dir=proto`.

It needs only Python 3.
"""

import fnmatch
import os
import re
import sys


class Error(Exception):
    """A problem in the input. protoc prints the text and stops."""


################################################################################
# The request and the response of protoc, in the protobuf wire format. The plugin reads only the fields that
# it uses and skips the other fields. The field numbers are from descriptor.proto and plugin.proto, which
# protobuf does not change.


class Field:
    """The constants of FieldDescriptorProto."""

    (TYPE_DOUBLE, TYPE_FLOAT, TYPE_INT64, TYPE_UINT64, TYPE_INT32, TYPE_FIXED64, TYPE_FIXED32, TYPE_BOOL,
     TYPE_STRING, TYPE_GROUP, TYPE_MESSAGE, TYPE_BYTES, TYPE_UINT32, TYPE_ENUM, TYPE_SFIXED32, TYPE_SFIXED64,
     TYPE_SINT32, TYPE_SINT64) = range(1, 19)
    LABEL_REPEATED = 3


# For each message: field number -> (attribute, kind, repeated). A kind is "string", "int" (int32), "bool"
# or the schema of a message.
ONEOF_DESC = {1: ("name", "string", False)}
ENUM_VALUE_DESC = {1: ("name", "string", False), 2: ("number", "int", False)}
ENUM_DESC = {1: ("name", "string", False), 2: ("value", ENUM_VALUE_DESC, True)}
MESSAGE_OPTIONS = {7: ("map_entry", "bool", False)}
FIELD_DESC = {1: ("name", "string", False), 3: ("number", "int", False), 4: ("label", "int", False),
              5: ("type", "int", False), 6: ("type_name", "string", False), 9: ("oneof_index", "int", False),
              17: ("proto3_optional", "bool", False)}
MESSAGE_DESC = {1: ("name", "string", False), 2: ("field", FIELD_DESC, True), 4: ("enum_type", ENUM_DESC, True),
                7: ("options", MESSAGE_OPTIONS, False), 8: ("oneof_decl", ONEOF_DESC, True)}
MESSAGE_DESC[3] = ("nested_type", MESSAGE_DESC, True)
FILE_DESC = {1: ("name", "string", False), 2: ("package", "string", False), 4: ("message_type", MESSAGE_DESC, True),
             5: ("enum_type", ENUM_DESC, True), 12: ("syntax", "string", False)}
REQUEST = {1: ("file_to_generate", "string", True), 2: ("parameter", "string", False),
           15: ("proto_file", FILE_DESC, True)}

FEATURE_PROTO3_OPTIONAL = 1


class Message:
    """A decoded message: one attribute for each field of its schema. `present` has the names of the fields that
    are not repeated and are in the input."""

    def __init__(self, schema):
        self.present = set()
        for name, kind, repeated in schema.values():
            if repeated:
                setattr(self, name, [])
            else:
                setattr(self, name, {"string": "", "int": 0, "bool": False}[kind] if isinstance(kind, str) else
                        Message(kind))


def invalid_request():
    return Error("the request from protoc is not valid protobuf")


def read_varint(data, pos):
    """The varint at `pos` in `data` and the position after it."""
    value = 0
    for shift in range(0, 70, 7):
        if pos >= len(data):
            raise invalid_request()
        value |= (data[pos] & 0x7F) << shift
        pos += 1
        if data[pos - 1] < 0x80:
            return value & 0xFFFFFFFFFFFFFFFF, pos
    raise invalid_request()


def decode(data, schema):
    """The message in `data` with the fields of `schema`."""
    message = Message(schema)
    pos = 0
    while pos < len(data):
        key, pos = read_varint(data, pos)
        number, wire_type = key >> 3, key & 7
        if wire_type == 0:
            value, pos = read_varint(data, pos)
        elif wire_type == 1 or wire_type == 5:
            value, pos = None, pos + (8 if wire_type == 1 else 4)
        elif wire_type == 2:
            size, pos = read_varint(data, pos)
            value, pos = data[pos:pos + size], pos + size
        else:
            raise invalid_request()
        if pos > len(data):
            raise invalid_request()
        if number not in schema:
            continue

        name, kind, repeated = schema[number]
        if wire_type != (0 if kind in ("int", "bool") else 2):
            raise invalid_request()
        if kind == "int":
            value = value - (1 << 64) if value >> 63 else value  # a negative int32 is a 10-byte varint
        elif kind == "bool":
            value = value != 0
        elif kind == "string":
            try:
                value = value.decode("utf-8")
            except UnicodeDecodeError:
                raise invalid_request() from None
        else:
            value = decode(value, kind)

        if repeated:
            getattr(message, name).append(value)
        else:
            setattr(message, name, value)
            message.present.add(name)
    return message


def varint(value):
    out = bytearray()
    while value >= 0x80:
        out.append(value & 0x7F | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def length_delimited(number, data):
    return varint(number << 3 | 2) + varint(len(data)) + data


def encode_response(error, files):
    """The CodeGeneratorResponse with `error` (or None) and `files`, each a pair of name and content."""
    out = length_delimited(1, error.encode("utf-8")) if error is not None else b""
    out += varint(2 << 3) + varint(FEATURE_PROTO3_OPTIONAL)
    for name, content in files:
        out += length_delimited(15, length_delimited(1, name.encode("utf-8")) +
                                length_delimited(15, content.encode("utf-8")))
    return out


SCALARS = {
    Field.TYPE_BOOL: "bool",
    Field.TYPE_INT32: "int32_t",
    Field.TYPE_INT64: "int64_t",
    Field.TYPE_UINT32: "uint32_t",
    Field.TYPE_UINT64: "uint64_t",
    Field.TYPE_SINT32: "int32_t",
    Field.TYPE_SINT64: "int64_t",
    Field.TYPE_FIXED32: "uint32_t",
    Field.TYPE_FIXED64: "uint64_t",
    Field.TYPE_SFIXED32: "int32_t",
    Field.TYPE_SFIXED64: "int64_t",
    Field.TYPE_FLOAT: "float",
    Field.TYPE_DOUBLE: "double",
    Field.TYPE_STRING: "std::string_view",
    Field.TYPE_BYTES: "std::span<const uint8_t>",
}

SCALAR_DEFAULTS = {
    Field.TYPE_BOOL: "false",
    Field.TYPE_INT32: "0",
    Field.TYPE_INT64: "0",
    Field.TYPE_UINT32: "0",
    Field.TYPE_UINT64: "0",
    Field.TYPE_SINT32: "0",
    Field.TYPE_SINT64: "0",
    Field.TYPE_FIXED32: "0",
    Field.TYPE_FIXED64: "0",
    Field.TYPE_SFIXED32: "0",
    Field.TYPE_SFIXED64: "0",
    Field.TYPE_FLOAT: "0.0f",
    Field.TYPE_DOUBLE: "0.0",
}

ENCODINGS = {
    Field.TYPE_SINT32: "pb::int_encoding::ZIGZAG",
    Field.TYPE_SINT64: "pb::int_encoding::ZIGZAG",
    Field.TYPE_FIXED32: "pb::int_encoding::FIXED",
    Field.TYPE_FIXED64: "pb::int_encoding::FIXED",
    Field.TYPE_SFIXED32: "pb::int_encoding::FIXED",
    Field.TYPE_SFIXED64: "pb::int_encoding::FIXED",
}

# The types that `int_size` can change.
SIZED_INTEGERS = (Field.TYPE_INT32, Field.TYPE_INT64, Field.TYPE_UINT32, Field.TYPE_UINT64,
                  Field.TYPE_SINT32, Field.TYPE_SINT64)

# The headers that the generated text can need, each with a pattern that shows the need.
INCLUDES = [
    ("<array>", r"std::array<"),
    ("<optional>", r"std::optional<"),
    ("<span>", r"std::span<"),
    ("<stddef.h>", r"\b(offsetof\(|size_t\b)"),
    ("<stdint.h>", r"\bu?int\d+_t\b"),
    ("<string_view>", r"std::string_view\b"),
]

KEYWORDS = set("""
    alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class
    compl concept const consteval constexpr constinit const_cast continue contract_assert co_await co_return
    co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for
    friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private
    protected public register reinterpret_cast requires return short signed sizeof static static_assert
    static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned
    using virtual void volatile wchar_t while xor xor_eq""".split())

# Names that the generated code or a standard header uses: the two namespaces, <stdint.h>/<stddef.h> types, and macros.
RESERVED = KEYWORDS | set(
    "pb std NULL EOF errno stdin stdout stderr assert offsetof "
    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t size_t ptrdiff_t".split()
)


def ident(name):
    """`name` as a C++ identifier: a keyword or a reserved name gets the suffix `_`."""
    return name + "_" if name in RESERVED else name


def namespace_of(package):
    """`a.b` gives `a::b`."""
    return "::".join(ident(part) for part in package.split(".") if part)


def stem(proto_name):
    """`dir/app.proto` gives `dir/app`."""
    return re.sub(r"\.proto$", "", proto_name)


def screaming_case(name):
    """`RxMode` and `rx_mode` give `RX_MODE`."""
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name).upper()


################################################################################


def positive(text):
    if not re.fullmatch(r"[0-9]{1,9}", text) or int(text) == 0:
        raise ValueError("a number from 1 to 999999999")
    return int(text)


def boolean(text):
    if text not in ("true", "false"):
        raise ValueError("true or false")
    return text == "true"


def field_type(text):
    """`true` for FT_POINTER."""
    if text not in ("FT_DEFAULT", "FT_STATIC", "FT_POINTER"):
        raise ValueError("FT_DEFAULT, FT_STATIC or FT_POINTER")
    return text == "FT_POINTER"


def int_size(text):
    sizes = {"IS_DEFAULT": 0, "IS_8": 8, "IS_16": 16, "IS_32": 32, "IS_64": 64}
    if text not in sizes:
        raise ValueError("IS_8, IS_16, IS_32 or IS_64")
    return sizes[text]


class Rule:
    """One line of an options file."""

    def __init__(self, line, pattern, values):
        self.line = line
        self.pattern = pattern
        self.values = values
        self.exact = not re.search(r"[*?\[]", pattern)
        self.matched = False


class Options:
    """The rules of one options file. `path` is None when the .proto file has no options file."""

    PARSERS = {"max_count": positive, "max_size": positive, "max_length": positive, "fixed_count": boolean,
               "fixed_length": boolean, "int_size": int_size, "enum_intsize": int_size, "type": field_type}

    def __init__(self, path):
        self.path = path
        self.rules = []
        self.warnings = []
        if path:
            try:
                with open(path, encoding="utf-8-sig") as file:
                    lines = file.readlines()
            except UnicodeDecodeError:
                raise Error(f"{path}: the file is not UTF-8 text") from None
            except OSError as problem:
                raise Error(f"{path}: {problem.strerror}") from None
            for number, line in enumerate(lines, 1):
                self.add(number, re.sub(r"(#|//).*", "", line).split(None, 1))

    def add(self, number, parts):
        """Adds the rule of line `number`. `parts` is the name pattern and the remaining text."""
        if not parts:
            return

        rest = parts[1] if len(parts) == 2 else ""
        pair = r"(\w+)\s*:\s*(\S+)"
        if not re.fullmatch(rf"({pair}\s*)+", rest):
            raise Error(f"{self.path}:{number}: expected <name pattern> <option>:<value> ...")

        values = {}
        for option, text in re.findall(pair, rest):
            if option not in self.PARSERS:
                self.warnings.append(f"{self.path}:{number}: warning: the unknown option '{option}' is ignored")
                continue
            try:
                values[option] = self.PARSERS[option](text)
            except ValueError as problem:
                raise Error(f"{self.path}:{number}: {option} needs {problem}") from None
        self.rules.append(Rule(number, parts[0], values))

    def of(self, name, package):
        """The options of the field with the full name `name`: option -> (value, rule). The last rule wins."""
        short = name[len(package) + 1:] if package else name
        found = {}
        for rule in self.rules:
            if fnmatch.fnmatchcase(name, rule.pattern) or fnmatch.fnmatchcase(short, rule.pattern):
                rule.matched = True
                found.update({option: (value, rule) for option, value in rule.values.items()})
        return found

    def check_use(self, name, found, used):
        """Warns about each option of `found` that the field `name` did not use. A wildcard rule is silent."""
        for option, (_, rule) in found.items():
            if option not in used and rule.exact:
                self.warnings.append(f"{self.path}:{rule.line}: warning: {option} does not apply to {name}")

    def check_rules(self):
        """Warns about each rule that matched no field."""
        for rule in self.rules:
            if not rule.matched:
                self.warnings.append(f"{self.path}:{rule.line}: warning: '{rule.pattern}' matches no field")


def find_options(proto_name, folders):
    """The path of the options file for `proto_name`, or None."""
    for folder in [""] + folders:
        path = os.path.join(folder, stem(proto_name) + ".options")
        if os.path.isfile(path):
            return path
    return None


################################################################################


class Type:
    """A message or an enum. A nested type `Outer.Inner` has the C++ name `Outer_Inner`."""

    def __init__(self, file, path, desc, is_enum):
        self.file = file
        self.desc = desc
        self.is_enum = is_enum
        self.proto_name = ".".join(filter(None, [file.package] + path))
        self.name = ident("_".join(path))


def types_of(file):
    """The enums and messages of `file` in the order of the file. Nested types are included."""
    found = []

    def visit(desc, path, is_enum):
        path = path + [desc.name]
        found.append(Type(file, path, desc, is_enum))
        if not is_enum:
            for nested in desc.enum_type:
                visit(nested, path, True)
            for nested in desc.nested_type:
                visit(nested, path, False)

    for desc in file.enum_type:
        visit(desc, [], True)
    for desc in file.message_type:
        visit(desc, [], False)
    return found


def enum_names(desc):
    """The C++ enumerator names of `desc`. The values lose the prefix `<ENUM NAME>_` when each value has it."""
    prefix = screaming_case(desc.name) + "_"
    names = [value.name for value in desc.value]
    if all(name.upper().startswith(prefix) and re.fullmatch(r"[A-Za-z]\w*", name[len(prefix):]) for name in names):
        names = [name[len(prefix):] for name in names]
    return [ident(name) for name in names]


def enum_text(enum, options):
    """The definition of an enum."""
    found = options.of(enum.proto_name, enum.file.package)
    bits = (found["enum_intsize"][0] if "enum_intsize" in found else 0) or 32  # IS_DEFAULT is 0: the default, 32
    options.check_use(enum.proto_name, found, {"enum_intsize"})
    if bits == 64:
        raise Error(f"{enum.file.name}: {enum.proto_name}: enum_intsize:IS_64 is not possible, a proto3 enum is int32")
    for value in enum.desc.value:
        fits = next(size for size in (8, 16, 32) if -(1 << (size - 1)) <= value.number < 1 << (size - 1))
        if fits > bits:
            raise Error(f"{enum.file.name}: {enum.proto_name}: the value {value.number} does not fit "
                        f"enum_intsize:IS_{bits}, IS_{fits} is the smallest")

    lines = [f"enum class {enum.name} : int{bits}_t {{"]
    lines += [f"    {name} = {value.number}," for name, value in zip(enum_names(enum.desc), enum.desc.value)]
    return "\n".join(lines + ["};"])


class Header:
    """The header for one .proto file. `types` has each type of the request by its full name."""

    def __init__(self, file, types, options):
        self.file = file
        self.types = types
        self.options = options
        self.imports = set()
        self.field_lists = []

    def error(self, text):
        return Error(f"{self.file.name}: {text}")

    def reference(self, target, member_names):
        """The name of the type `target` in a struct that has the members `member_names`. Also records the
        file of `target` as an import that the header uses."""
        if target.file.name != self.file.name:
            self.imports.add(target.file.name)
        if target.file.package == self.file.package and target.name not in member_names:
            return target.name
        return "::" + "::".join(filter(None, [namespace_of(target.file.package), target.name]))

    def field_type(self, message, field, member_names):
        """The C++ type for `field`, without `std::optional`."""
        name = f"{message.proto_name}.{field.name}"
        found = self.options.of(name, self.file.package)
        used = set()
        repeated = field.label == Field.LABEL_REPEATED

        def option(key):
            used.add(key)
            return found[key][0] if key in found else None

        if field.type in (Field.TYPE_MESSAGE, Field.TYPE_ENUM):
            target = self.types[field.type_name]
            if not target.is_enum and target.desc.options.map_entry:
                key_field = next(f for f in target.desc.field if f.number == 1)
                val_field = next(f for f in target.desc.field if f.number == 2)
                key_t = self.field_type(target, key_field, member_names)
                val_t = self.field_type(target, val_field, member_names)
                key_enc = ENCODINGS.get(key_field.type)
                val_enc = ENCODINGS.get(val_field.type)
                enc_args = []
                if key_enc or val_enc:
                    enc_args.append(key_enc or "pb::int_encoding::VARINT")
                    if val_enc:
                        enc_args.append(val_enc)
                pointer = option("type")
                count = option("max_count") if not pointer else None
                if not pointer and not count and option("fixed_count"):
                    raise self.error(f"{name}: fixed_count needs max_count")
                pair_args = ", ".join([key_t, val_t] + enc_args)
                if count and option("fixed_count"):
                    text = f"std::array<pb::kv_pair<{pair_args}>, {count}>"
                elif count:
                    map_args = ", ".join([key_t, val_t, str(count)] + enc_args)
                    text = f"pb::map<{map_args}>"
                else:
                    text = f"std::span<const pb::kv_pair<{pair_args}>>"
                self.options.check_use(name, found, used)
                return text
            text = self.reference(target, member_names)
        else:
            text = SCALARS[field.type]

        if field.type in SIZED_INTEGERS:
            bits = option("int_size")
            if bits:
                text = re.sub(r"\d+", str(bits), text)
            if repeated and text == "uint8_t":
                raise self.error(f"{name}: int_size:IS_8 on a repeated unsigned field gives a bytes field")

        pointer = option("type") if repeated or field.type in (Field.TYPE_BYTES, Field.TYPE_STRING) else False

        if field.type == Field.TYPE_BYTES and not pointer:
            size, fixed = option("max_size"), option("fixed_length")
            if fixed and not size:
                raise self.error(f"{name}: fixed_length needs max_size")
            if size:
                text = f"std::array<uint8_t, {size}>" if fixed else f"pb::bytes<{size}>"
        elif field.type == Field.TYPE_STRING and not pointer:
            length, size = option("max_length"), option("max_size")
            if length is not None and size is not None:
                len_line, size_line = found["max_length"][1].line, found["max_size"][1].line
                if len_line == size_line:
                    raise self.error(f"{name}: give max_length or max_size, not both")
                chars = length if len_line > size_line else size - 1
            elif length is not None:
                chars = length
            elif size is not None:
                chars = size - 1
            else:
                chars = None
            if chars is not None:
                text = f"pb::string<{chars}>"

        count = option("max_count") if repeated and not pointer else None
        if repeated and not pointer and not count and option("fixed_count"):
            raise self.error(f"{name}: fixed_count needs max_count")
        if count and option("fixed_count"):
            text = f"std::array<{text}, {count}>"
        elif count:
            text = f"pb::repeated<{text}, {count}>"
        elif repeated:
            text = f"std::span<const {text}>"

        self.options.check_use(name, found, used)
        return text

    def struct_text(self, message):
        """The definition of the struct of `message`. Also makes the field list of the struct."""
        desc = message.desc

        def oneof_of(field):  # the index of the oneof of `field`. `optional` is a oneof only for protoc.
            return field.oneof_index if "oneof_index" in field.present and not field.proto3_optional else None

        all_names = ([ident(field.name) for field in desc.field if oneof_of(field) is None] +
                     [ident(desc.oneof_decl[idx].name) for idx in dict.fromkeys(
                         oneof_of(field) for field in desc.field if oneof_of(field) is not None)] +
                     [ident(f"{desc.oneof_decl[oneof_of(field)].name}_{field.name}")
                      for field in desc.field if oneof_of(field) is not None])
        for cname in all_names:
            if all_names.count(cname) > 1:
                raise self.error(f"{message.proto_name}: two members have the C++ name {cname}")
        member_names = set(all_names)

        lines, entries, done = [], [], set()  # an entry: member name, field number, alternative index, encoding
        for field in desc.field:
            index = oneof_of(field)
            if index is None:
                text = self.field_type(message, field, member_names)
                if field.proto3_optional:
                    text = f"std::optional<{text}>"
                    default = "{}"
                elif field.label == Field.LABEL_REPEATED:
                    default = "{}"
                elif field.type == Field.TYPE_ENUM:
                    enum_desc = self.types[field.type_name].desc
                    zero = next(name for name, value in zip(enum_names(enum_desc), enum_desc.value)
                                if value.number == 0)
                    default = f"{text}::{zero}"
                else:
                    default = SCALAR_DEFAULTS.get(field.type, "{}")
                lines.append(f"    {text} {ident(field.name)} = {default};")
                entries.append((ident(field.name), field.number, 0, ENCODINGS.get(field.type)))
            elif index not in done:
                done.add(index)
                group = [other for other in desc.field if oneof_of(other) == index]
                name = desc.oneof_decl[index].name
                types = [self.field_type(message, other, member_names) for other in group]
                for position, other in enumerate(group):
                    const_name = ident(f"{name}_{other.name}")
                    lines.append(f"    static constexpr size_t {const_name} = {position};")
                lines.append(f"    pb::oneof<{', '.join(types)}> {ident(name)} = {{}};")
                entries += [(ident(name), other.number, position, ENCODINGS.get(other.type))
                            for position, other in enumerate(group, 1)]

        self.field_lists.append(self.field_list_text(message, entries))
        if not lines:
            return f"struct {message.name} {{}};"
        return "\n".join([f"struct {message.name} {{"] + lines + ["};"])

    def field_list_text(self, message, entries):
        """The `pb::fields_of` specialization of `message`: the type, field number, offset and encoding."""
        # The full name with `::` at the start: this text is in the scope of `pb`, which has names of its own.
        name = "::" + "::".join(filter(None, [namespace_of(self.file.package), message.name]))
        lines = [f"template <>\nstruct pb::fields_of<{name}> : pb::field_list<{name}"]
        for member, number, alternative, encoding in entries:
            tail = f", {alternative}, {encoding}" if encoding else (f", {alternative}" if alternative else "")
            lines.append(f"    pb::field_at<decltype({name}::{member}), {number}, offsetof({name}, {member}){tail}>")
        return ",\n".join(lines) + "> {};"

    def in_dependency_order(self, messages):
        """`messages` in an order where each message comes after the messages of this file that it contains."""
        order, stack = [], []

        def visit(message):
            if message in stack:
                cycle = stack[stack.index(message):] + [message]
                raise self.error("a message cannot contain itself: " + " -> ".join(m.name for m in cycle))
            if message in order:
                return

            stack.append(message)
            for field in message.desc.field:
                if field.type == Field.TYPE_MESSAGE:
                    target = self.types[field.type_name]
                    if target.desc.options.map_entry:
                        val_field = next(f for f in target.desc.field if f.number == 2)
                        if val_field.type == Field.TYPE_MESSAGE:
                            val_target = self.types[val_field.type_name]
                            if val_target.file.name == self.file.name:
                                visit(val_target)
                    elif target.file.name == self.file.name:
                        visit(target)
            stack.pop()
            order.append(message)

        for message in messages:
            visit(message)
        return order

    def text(self):
        """The complete text of the header."""
        if self.file.syntax != "proto3":
            raise self.error("only proto3 files are supported")

        types = [each for each in self.types.values()
                 if each.file.name == self.file.name and (each.is_enum or not each.desc.options.map_entry)]
        names = [each.name for each in types]
        for name in names:
            if names.count(name) > 1:
                raise self.error(f"two types have the C++ name {name}")

        # The warning for a rule that matches no field must not get lost when an error comes later.
        messages = [each for each in types if not each.is_enum]
        for message in self.types.values():
            if message.file.name == self.file.name and not message.is_enum:
                for field in message.desc.field:
                    self.options.of(f"{message.proto_name}.{field.name}", self.file.package)
        for each in types:
            if each.is_enum:
                self.options.of(each.proto_name, self.file.package)
        self.options.check_rules()

        blocks = [enum_text(each, self.options) for each in types if each.is_enum]
        blocks += [self.struct_text(each) for each in self.in_dependency_order(messages)]
        body = "\n\n".join(blocks)
        lists = "\n\n".join(self.field_lists)

        out = [f"// Generated by protoc-gen-micropb from {self.file.name}. Do not edit.", "#pragma once", ""]
        includes = [f"#include {name}" for name, pattern in INCLUDES if re.search(pattern, body + lists)]
        if includes:
            out += includes + [""]
        if "pb::" in body + lists:
            out += ["#include <micropb/types.hpp>", ""]
        if self.imports:
            out += [f'#include "{stem(name)}.pb.hpp"' for name in sorted(self.imports)] + [""]

        namespace = namespace_of(self.file.package)
        if namespace:
            out += [f"namespace {namespace} {{", "", body, "", f"}} // namespace {namespace}"]
        else:
            out += [body]
        if lists:
            out += ["", "// Field lists.", "", lists]
        return "\n".join(out) + "\n"


################################################################################


def generate(request):
    """The encoded response for `request` and the warnings. An error gives a response without files."""
    headers, warnings = [], []
    try:
        folders = []
        for parameter in filter(None, request.parameter.split(",")):
            key, _, value = parameter.partition("=")
            if key != "options_dir" or not value:
                raise Error(f"unknown parameter '{parameter}'")
            folders.append(value)

        files = {file.name: file for file in request.proto_file}
        types = {"." + each.proto_name: each for file in request.proto_file for each in types_of(file)}
        for name in request.file_to_generate:
            options = Options(find_options(name, folders))
            try:
                text = Header(files[name], types, options).text()
            finally:
                warnings += options.warnings
            headers.append((stem(name) + ".pb.hpp", text))
    except Error as error:
        return encode_response(str(error), []), warnings
    return encode_response(None, headers), warnings


def main():
    try:
        request = decode(sys.stdin.buffer.read(), REQUEST)
    except Error as error:
        response, warnings = encode_response(str(error), []), []
    else:
        response, warnings = generate(request)
    for warning in warnings:
        print(warning, file=sys.stderr)
    sys.stdout.buffer.write(response)


if __name__ == "__main__":
    main()
