TARGET   = menu_view_ab
SRC_CC   = main.cc
LIBS     = base libc libm vfs libpng zlib blit file
PRG_MENU_DIR = $(PRG_DIR)/../menu_view
INC_DIR += $(PRG_MENU_DIR)

CUSTOM_TARGET_DEPS += menu_view_styles.tar

menu_view_styles.tar:
	$(VERBOSE)cd $(PRG_MENU_DIR); tar cf $(PWD)/bin/$@ styles
