RNG=$(top_srcdir)/xml/interface.rng

xmllint:
	@(for f in interface/*.xml; do                       \
	    if [ $$(basename $$f) != "schemas.xml" ] ; then  \
	      xmllint -relaxng $(RNG) -noout $$f             \
	        || echo FAIL;                                \
            fi;                                              \
	  done)
