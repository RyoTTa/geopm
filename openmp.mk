#  Copyright (c) 2015, 2016, 2017, 2018, 2019, Intel Corporation
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in
#        the documentation and/or other materials provided with the
#        distribution.
#
#      * Neither the name of Intel Corporation nor the names of its
#        contributors may be used to endorse or promote products derived
#        from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

if ENABLE_OMPT
if !HAS_OMPT

pkglibdir ?= $(libdir)/geopm

AM_CPPFLAGS += -Iopenmp/include
AM_LDFLAGS += -Lopenmp/lib

BUILT_SOURCES += openmp/lib/libiomp5.so \
                 openmp/include/omp.h \
                 openmp/include/omp-tools.h \
                 # end

EXTRA_DIST += $(openmp_archive)
DISTCLEANFILES += openmp/VERSION \
                  $(openmp_archive) \
                  # end

dist-openmp: openmp_archive_check

openmp_src = openmp-$(openmp_version).src/runtime/runtime/src
openmp_version = 8.0.1
openmp_archive = openmp-$(openmp_version).src.tar.xz
openmp_url = https://github.com/llvm/llvm-project/releases/download/llvmorg-$(openmp_version)/openmp-$(openmp_version).src.tar.xz
openmp_sha1 = 64fc83d7ba5be944835961827acad93fd36a9c0c
openmp_so = $(openmp_src)/libomp.so

$(openmp_archive):
	wget --timeout=20 $(openmp_url) || \
	curl --connect-timeout 20 -O $(openmp_url) || \
	echo "Warning: Unable to download OpenMP archive" 2>&1 && \
	touch $@

openmp_archive_check: $(openmp_archive)
	@if [ ! -s $^ ]; then \
	    echo "Error: The OpenMP archive is empty" 2>&1; \
	    exit -1; \
	fi
	@if [ $$(sha1sum $^ | awk '{print $$1}') != $(openmp_sha1) ]; then \
	    echo "Error: The OpenMP archive does not have the correct SHA-1 checksum" 2>&1; \
	    exit -1; \
	fi
	@echo '[ PASSED ] openmp_archive_check'

openmp/VERSION: $(openmp_archive)
	@if [ ! -s $^ ]; then \
	    mkdir -p openmp; \
	    touch $@; \
	elif [ $$(sha1sum $^ | awk '{print $$1}') != $(openmp_sha1) ]; then \
	    echo "Error: The OpenMP archive does not have the correct SHA-1 checksum" 2>&1; \
	    exit -1; \
	else \
	    rm -rf openmp; \
	    tar xf $^; \
	    mkdir -p openmp; \
	    echo $(openmp_version) > $@; \
	fi

$(openmp_so): openmp/VERSION
	@if [ ! -s $^ ]; then \
	    echo "Error: Failure to extract or download OpenMP archive" 2>&1; \
	    exit -1; \
	fi
	cd openmp-$(openmp_version).src/runtime && \
	cmake -DLIBOMP_OMPT_SUPPORT=on -DCMAKE_C_COMPILER=$(CC) -DCMAKE_CXX_COMPILER=$(CXX) -Wno-dev .. && \
	make

openmp/lib/libiomp5.so: $(openmp_so)
	mkdir -p openmp/lib
	cp $(openmp_so) openmp/lib/libomp.so
	ln -sf libomp.so openmp/lib/libiomp5.so
	mkdir -p openmp/include

openmp/include/omp.h: openmp/lib/libiomp5.so
	cp $(openmp_src)/omp.h openmp/include/omp.h

openmp/include/omp-tools.h: openmp/lib/libiomp5.so
	cp $(openmp_src)/omp-tools.h openmp/include/omp-tools.h

install-openmp: openmp/lib/libiomp5.so
	$(INSTALL) -d $(DESTDIR)/$(pkglibdir)/openmp/lib
	$(INSTALL) openmp/lib/libomp.so $(DESTDIR)/$(pkglibdir)/openmp/lib
	$(INSTALL) openmp/lib/libiomp5.so $(DESTDIR)/$(pkglibdir)/openmp/lib

clean-local-openmp:
	rm -rf openmp
	rm -rf openmp-$(openmp_version).src

endif # !HAS_OMPT
endif # ENABLE_OMPT

if !ENABLE_OMPT
install-openmp:
	-
dist-openmp:
	-
clean-local-openmp:
	-
else
if HAS_OMPT
install-openmp:
	-
dist-openmp:
	-
clean-local-openmp:
	-
endif
endif
