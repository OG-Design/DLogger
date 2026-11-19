# Dependencies

- libcurl
- systemd
- gcc
- make
- openssl

```sh
# fedora
# lib curl
sudo dnf install libcurl-devel

# lib systemd
sudo dnf install systemd-devel

# openssl for encryption of smtp password
sudo dnf install openssl
```

# Build & Run

```sh
# build the binary
./build.sh

# or
make

# output in
./dist/out

# run
./run.sh

# build & run together
./dev.sh

```