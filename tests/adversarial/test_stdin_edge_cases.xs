-- this file exists so `bash tests/run.sh` runs it. The real /dev/stdin
-- test has to be a shell script (below) because the XS file itself
-- needs to be the input piped to `xs`. Here we just verify the
-- interpreter can handle empty and minimal programs.

println("ok")
