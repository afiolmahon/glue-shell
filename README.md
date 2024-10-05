# Glue Shell

## Installation

```bash
# Clone repo and submodules
git clone git@github.com:afiolmahon/glue-shell.git
cd ./glue-shell
git submodule update --init --recursive

# Configure Build
mkdir build
cmake -B build/ -S ./

# Build
cmake --build build/
# or
cd build/ && make
```
