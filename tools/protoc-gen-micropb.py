#!/usr/bin/env python3
"""protoc plugin: writes C++26 structs for micropb from a proto3 file.

    protoc --plugin=protoc-gen-micropb=tools/protoc-gen-micropb.py --micropb_out=gen app.proto

The output is gen/app.pb.hpp. The sizes of repeated fields come from app.options, a file in the
NanoPB format in the folder of app.proto:

    # <name pattern> <option>:<value> ...
    Message.field  max_count:8                    pb::repeated<T, 8>
    Message.field  max_count:3 fixed_count:true   std::array<T, 3>
    Message.field  max_size:16                    bytes: pb::repeated<std::uint8_t, 16>
    Message.field  max_size:6 fixed_length:true   bytes: std::array<std::uint8_t, 6>
    Message.field  int_size:IS_8                  std::int8_t or std::uint8_t (also IS_16, IS_32, IS_64)

The plugin looks for the options file from the current folder and from each folder of the
parameter --micropb_opt=options_dir=<folder>. It needs Python 3 and the Python package protobuf.
"""

import fnmatch
import os
import re
import sys

from google.protobuf.compiler import plugin_pb2
from google.protobuf.descriptor_pb2 import FieldDescriptorProto as Field


class Error(Exception):
    """A problem in the input. protoc prints the text and stops."""


SCALARS = {
    Field.TYPE_BOOL: "bool",
    Field.TYPE_INT32: "std::int32_t",
    Field.TYPE_INT64: "std::int64_t",
    Field.TYPE_UINT32: "std::uint32_t",
    Field.TYPE_UINT64: "std::uint64_t",
    Field.TYPE_SINT32: "pb::sint32",
    Field.TYPE_SINT64: "pb::sint64",
    Field.TYPE_FIXED32: "pb::fixed32",
    Field.TYPE_FIXED64: "pb::fixed64",
    Field.TYPE_SFIXED32: "pb::sfixed32",
    Field.TYPE_SFIXED64: "pb::sfixed64",
    Field.TYPE_FLOAT: "float",
    Field.TYPE_DOUBLE: "double",
    Field.TYPE_STRING: "std::string_view",
    Field.TYPE_BYTES: "std::span<const std::uint8_t>",
}

# The types that `int_size` can change.
SIZED_INTEGERS = (Field.TYPE_INT32, Field.TYPE_INT64, Field.TYPE_UINT32, Field.TYPE_UINT64)

# The headers that the generated text can need, each with a pattern that shows the need.
INCLUDES = [
    ("<array>", r"std::array<"),
    ("<cstdint>", r"std::u?int\d+_t"),
    ("<optional>", r"std::optional<"),
    ("<span>", r"std::span<"),
    ("<string_view>", r"std::string_view"),
    ("<variant>", r"std::variant<"),
]

KEYWORDS = set("""
    alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class
    compl concept const consteval constexpr constinit const_cast continue contract_assert co_await co_return
    co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for
    friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private
    protected public register reinterpret_cast requires return short signed sizeof static static_assert
    static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned
    using virtual void volatile wchar_t while xor xor_eq""".split())


def ident(name):
    """`name` as a C++ identifier: a keyword gets the suffix `_`."""
    return name + "_" if name in KEYWORDS else name


def screaming_case(name):
    """`RxMode` and `rx_mode` give `RX_MODE`."""
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name).upper()


################################################################################


def positive(text):
    if not text.isdigit() or int(text) == 0:
        raise ValueError("a number above 0")
    return int(text)


def boolean(text):
    if text not in ("true", "false"):
        raise ValueError("true or false")
    return text == "true"


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

    PARSERS = {"max_count": positive, "max_size": positive, "fixed_count": boolean, "fixed_length": boolean,
               "int_size": int_size}

    def __init__(self, path):
        self.path = path
        self.rules = []
        self.warnings = []
        if path:
            with open(path, encoding="utf-8") as file:
                for number, line in enumerate(file, 1):
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
        path = os.path.join(folder, re.sub(r"\.proto$", "", proto_name) + ".options")
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


def enum_text(enum):
    """The definition of an enum. The values lose the prefix `<ENUM NAME>_` when each value has it."""
    prefix = screaming_case(enum.desc.name) + "_"
    names = [value.name for value in enum.desc.value]
    if all(name.upper().startswith(prefix) and name[len(prefix):].isidentifier() for name in names):
        names = [name[len(prefix):] for name in names]

    lines = [f"enum class {enum.name} : std::int32_t {{"]
    lines += [f"    {ident(name)} = {value.number}," for name, value in zip(names, enum.desc.value)]
    return "\n".join(lines + ["};"])


class Header:
    """The header for one .proto file. `types` has each type of the request by its full name."""

    def __init__(self, file, types, options):
        self.file = file
        self.types = types
        self.options = options
        self.imports = set()

    def error(self, text):
        return Error(f"{self.file.name}: {text}")

    def reference(self, target, member_names):
        """The name of the type `target` in a struct that has the members `member_names`."""
        if target.file.name != self.file.name:
            self.imports.add(target.file.name)
        if target.file.package == self.file.package and target.name not in member_names:
            return target.name
        return "::" + "::".join(filter(None, target.file.package.split(".") + [target.name]))

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
                raise self.error(f"{name}: map fields are not supported")
            text = self.reference(target, member_names)
        else:
            text = SCALARS[field.type]

        if field.type in SIZED_INTEGERS:
            bits = option("int_size")
            if bits:
                text = re.sub(r"\d+", str(bits), text)
            if repeated and text == "std::uint8_t":
                raise self.error(f"{name}: int_size:IS_8 on a repeated unsigned field gives a bytes field")

        if field.type == Field.TYPE_BYTES and not repeated:
            size, fixed = option("max_size"), option("fixed_length")
            if fixed and not size:
                raise self.error(f"{name}: fixed_length needs max_size")
            if size:
                text = f"std::array<std::uint8_t, {size}>" if fixed else f"pb::repeated<std::uint8_t, {size}>"

        if repeated:
            count, fixed = option("max_count"), option("fixed_count")
            if not count:
                file = self.options.path or re.sub(r"\.proto$", "", self.file.name) + ".options"
                hint = "" if self.options.path else (f". No file {file} was found. If it is in another folder, "
                                                     "use --micropb_opt=options_dir=<folder>.")
                raise self.error(f"{name}: a repeated field needs a capacity. "
                                 f"Add this line to {file}: {name} max_count:<N>{hint}")
            text = f"std::array<{text}, {count}>" if fixed else f"pb::repeated<{text}, {count}>"

        self.options.check_use(name, found, used)
        return text

    def struct_text(self, message):
        """The definition of the struct of `message`. A member has a number only where the numbers have a gap."""
        desc = message.desc
        if not desc.field:
            return f"struct {message.name} {{}};"

        def oneof_of(field):  # the index of the oneof of `field`. `optional` is a oneof only for protoc.
            return field.oneof_index if field.HasField("oneof_index") and not field.proto3_optional else None

        member_names = {ident(field.name) for field in desc.field if oneof_of(field) is None}
        member_names |= {ident(desc.oneof_decl[oneof_of(field)].name) for field in desc.field
                         if oneof_of(field) is not None}

        lines, number, done = [f"struct {message.name} {{"], 1, set()
        for field in desc.field:
            index = oneof_of(field)
            if index is None:
                text = self.field_type(message, field, member_names)
                if field.proto3_optional:
                    text = f"std::optional<{text}>"
                tag = "" if field.number == number else f"[[= pb::tag{{{field.number}}}]] "
                lines.append(f"    {tag}{text} {ident(field.name)};")
                number = field.number + 1
            elif index not in done:
                done.add(index)
                group = [other for other in desc.field if oneof_of(other) == index]
                name = desc.oneof_decl[index].name
                types = [self.field_type(message, other, member_names) for other in group]
                for text in types:
                    if types.count(text) > 1:
                        raise self.error(f"{message.proto_name}.{name}: a oneof cannot have two members "
                                         f"of the type {text}")
                numbers = [other.number for other in group]
                tags = ", ".join(str(n) for n in numbers)
                tag = "" if numbers == list(range(number, number + len(group))) else f"[[= pb::oneof_tags<{tags}>{{}}]] "
                lines.append(f"    {tag}std::variant<std::monostate, {', '.join(types)}> {ident(name)};")
                number = numbers[-1] + 1
        return "\n".join(lines + ["};"])

    def sorted(self, messages):
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
                if field.type == Field.TYPE_MESSAGE and self.types[field.type_name].file.name == self.file.name:
                    visit(self.types[field.type_name])
            stack.pop()
            order.append(message)

        for message in messages:
            visit(message)
        return order

    def text(self):
        """The complete text of the header."""
        if self.file.syntax != "proto3":
            raise self.error("only proto3 files are supported")

        types = [each for each in self.types.values() if each.file.name == self.file.name]
        names = [each.name for each in types]
        for name in names:
            if names.count(name) > 1:
                raise self.error(f"two types have the C++ name {name}")

        blocks = [enum_text(each) for each in types if each.is_enum]
        blocks += [self.struct_text(each) for each in self.sorted([each for each in types if not each.is_enum])]
        body = "\n\n".join(blocks)
        self.options.check_rules()

        out = [f"// Generated by protoc-gen-micropb from {self.file.name}. Do not edit.", "#pragma once", ""]
        includes = [f"#include {name}" for name, pattern in INCLUDES if re.search(pattern, body)]
        if includes:
            out += includes + [""]
        if "pb::" in body:
            out += ["#include <micropb/types.hpp>", ""]
        if self.imports:
            out += [f'#include "{re.sub(r"\.proto$", "", name)}.pb.hpp"' for name in sorted(self.imports)] + [""]

        namespace = self.file.package.replace(".", "::")
        if namespace:
            out += [f"namespace {namespace} {{", "", body, "", f"}} // namespace {namespace}"]
        else:
            out += [body]
        return "\n".join(out) + "\n"


################################################################################


def generate(request):
    """The response for `request` and the warnings. An error gives a response without files."""
    response = plugin_pb2.CodeGeneratorResponse()
    response.supported_features = plugin_pb2.CodeGeneratorResponse.FEATURE_PROTO3_OPTIONAL
    warnings = []
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
            text = Header(files[name], types, options).text()
            response.file.add(name=re.sub(r"\.proto$", "", name) + ".pb.hpp", content=text)
            warnings += options.warnings
    except Error as error:
        response.ClearField("file")
        response.error = str(error)
    return response, warnings


def main():
    request = plugin_pb2.CodeGeneratorRequest.FromString(sys.stdin.buffer.read())
    response, warnings = generate(request)
    for warning in warnings:
        print(warning, file=sys.stderr)
    sys.stdout.buffer.write(response.SerializeToString())


if __name__ == "__main__":
    main()
