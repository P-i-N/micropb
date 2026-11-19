@echo off
rem Starts protoc-gen-micropb.py, because protoc cannot start a .py file on Windows.
rem protoc --plugin=protoc-gen-micropb=tools\protoc-gen-micropb.bat --micropb_out=gen app.proto
python "%~dp0protoc-gen-micropb.py" %*
