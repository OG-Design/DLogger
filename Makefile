CFLAGS=-Wall -Wextra -std=c11 -g
SYSTEMD_CFLAGS=$(shell pkg-config --cflags libsystemd)
SYSTEMD_LIBS=$(shell pkg-config --libs libsystemd)

outdir=./dist/out

srcdir=./src

main_o=main.o
samba_o=samba.o
cjson_o=cjson.o

sambadir=$(srcdir)/samba
samba_c=samba.c
samba_src=$(sambadir)/$(samba_c)

cjsondir=./third_party/cjson
cjson_c=cjson.c
cjson_src=$(cjsondir)/$(cjson_c)
cjson_out=$(outdir)/$(cjson_o)

systemd_o=systemd.o
systemddir=$(srcdir)/systemd
systemd_c=systemd.c
systemd_src=$(systemddir)/$(systemd_c)
systemd_out=$(outdir)/systemd_o

utils_c=utils.c
utils_src=$(srcdir)/$(utils_c)
utils_o=utils.o
utils_out=$(outdir)/$(utils_o)


main_out=$(outdir)/$(main_o)
samba_out=$(outdir)/$(samba_o)

.PHONY: all clean

all: main

# This is a comment
main: $(main_out) $(samba_out) $(cjson_out) $(systemd_out) $(utils_out)
	gcc $(CFLAGS) $(main_out) $(samba_out) $(cjson_out) $(systemd_out) $(utils_out) $(SYSTEMD_LIBS) -o ./dist/out/main

$(main_out): ./src/main.c
	@mkdir -p $(outdir)
	gcc -c ./src/main.c -o $@

$(samba_out): $(samba_src)
	@mkdir -p $(outdir)
	gcc -c $(samba_src) -o $@

$(cjson_out): $(cjson_src)
	@mkdir -p $(outdir)
	gcc -c $(cjson_src) -o $@

$(systemd_out): $(systemd_src)
	@mkdir -p $(outdir)
	gcc -c $(systemd_src) $(SYSTEMD_CFLAGS) -o $@

$(utils_out): $(utils_src)
	@mkdir -p $(outdir)
	gcc -c $(utils_src) -o $@


clean: 
	rm -rf ./dist main