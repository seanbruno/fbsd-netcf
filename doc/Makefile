RNG=../xml/interface.rng
all:

check:
	@(for f in examples/*.xml; do                        \
	    if [ "$$f" != "examples/schemas.xml" ] ; then    \
	      xmllint -relaxng $(RNG) -noout $$f             \
	        || echo FAIL;                                \
            fi;                                              \
	  done)
