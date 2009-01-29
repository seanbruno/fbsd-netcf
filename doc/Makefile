all: interface.rnc

%.rnc: %.rng
	trang $< $@

check:
	@(for f in examples/*.xml; do                        \
	    if [ "$$f" != "examples/schemas.xml" ] ; then    \
	      xmllint -relaxng interface.rng -noout $$f      \
	        || echo FAIL;                                \
            fi;                                              \
	  done)
