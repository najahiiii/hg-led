include $(TOPDIR)/rules.mk

PKG_NAME:=hgledon
PKG_RELEASE:=1
PKG_LICENSE:=MIT
PKG_MAINTAINER:=Najahi

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/hgledon
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=GPIO/LED control helper
endef

define Package/hgledon/description
Small helper to control board LEDs via GPIO sysfs.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	rsync -a --copy-links ./src/ $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-DHGLEDON_MAIN \
		-o $(PKG_BUILD_DIR)/hgledon \
		$(PKG_BUILD_DIR)/hgledon.c
endef

define Package/hgledon/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hgledon $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,hgledon))
