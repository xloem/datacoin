.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. datacoin_qt test_datacoin_qt
clean: FORCE
	$(MAKE) -C .. datacoin_qt_clean test_datacoin_qt_clean
check: FORCE
	$(MAKE) -C .. test_datacoin_qt_check
datacoin-qt datacoin-qt.exe: FORCE
	 $(MAKE) -C .. datacoin_qt
