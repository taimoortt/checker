.PHONY: all clean

all:
	./CONFIG/make_load-parameter-file.sh
	$(MAKE) -C Debug clean
	$(MAKE) -C Debug all
	ln -sfn Debug/LTE-Sim LTE-Sim

clean:
	rm -f LTE-Sim
	$(MAKE) -C Debug clean
