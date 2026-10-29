.PHONY: check shims initrd
check:
	python3 scripts/check-source.py
	python3 -m compileall -q lib scripts
	./droidvm --help >/dev/null
	@for script in scripts/*.sh; do sh -n "$$script"; done
shims:
	scripts/build-shims.sh
initrd:
	python3 scripts/make-initrd.py
