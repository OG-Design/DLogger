CFLAGS=-Wall -Wextra -std=c11 -g
SYSTEMD_CFLAGS=$(shell pkg-config --cflags libsystemd)
SYSTEMD_LIBS=$(shell pkg-config --libs libsystemd)
CURL_CFLAGS=$(shell pkg-config --cflags libcurl)
CURL_LIBS=$(shell pkg-config --libs libcurl)

appname=dlogger

outdir=./dist/out

srcdir=./src

main_o=main.o
samba_o=samba.o
cjson_o=cjson.o
reporter_o=reporter.o

sambadir=$(srcdir)/samba
samba_c=samba.c
samba_src=$(sambadir)/$(samba_c)

cjsondir=./third_party/cjson
cjson_c=cJSON.c
cjson_src=$(cjsondir)/$(cjson_c)
cjson_out=$(outdir)/$(cjson_o)

systemd_o=systemd.o
systemddir=$(srcdir)/systemd
systemd_c=systemd.c
systemd_src=$(systemddir)/$(systemd_c)
systemd_out=$(outdir)/$(systemd_o)

utils_c=utils.c
utils_src=$(srcdir)/$(utils_c)
utils_o=utils.o
utils_out=$(outdir)/$(utils_o)

smtp_src=$(srcdir)/smtp/smtp.c
smtp_out=$(outdir)/smtp.o


reporter_src=$(srcdir)/reporter/reporter.c
reporter_out=$(outdir)/$(reporter_o)



main_out=$(outdir)/$(main_o)
samba_out=$(outdir)/$(samba_o)

.PHONY: all clean

all: $(appname)

# This is a comment
$(appname): $(main_out) $(samba_out) $(cjson_out) $(systemd_out) $(utils_out) $(reporter_out) $(smtp_out)
	gcc $(CFLAGS) $(main_out) $(samba_out) $(cjson_out) $(systemd_out) $(utils_out) $(reporter_out) $(smtp_out) $(SYSTEMD_LIBS) $(CURL_LIBS) -o ./dist/out/$(appname)

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

$(reporter_out): $(reporter_src) $(srcdir)/reporter/reporter.h $(srcdir)/utils.h
	@mkdir -p $(outdir)
	gcc -c $(reporter_src) -o $@

$(smtp_out): $(smtp_src) 
	@mkdir -p $(outdir)
	gcc -c $(smtp_src) $(CURL_CFLAGS) -o $@


clean: 
	rm -rf ./dist $(appname)