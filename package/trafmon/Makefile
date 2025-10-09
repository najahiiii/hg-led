include $(TOPDIR)/rules.mk

PKG_NAME:=trafmon
PKG_RELEASE:=1
PKG_LICENSE:=MIT
PKG_MAINTAINER:=Najahi

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/trafmon
  SECTION:=net
  CATEGORY:=Network
  TITLE:=LED Traffic Monitor daemon (procd)
  DEPENDS:=+libpthread
endef

define Package/trafmon/description
Daemon that monitors a network interface and blinks a board LED based on RX/TX load.
It embeds hgledon routines for LED control.
endef

define Package/trafmon/conffiles
/etc/config/trafmon
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	rsync -a --copy-links ./src/ $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-o $(PKG_BUILD_DIR)/trafmon \
		$(PKG_BUILD_DIR)/trafmon.c \
		$(PKG_BUILD_DIR)/hgledon.c \
		-lm
endef

define Package/trafmon/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/trafmon $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/trafmon $(1)/etc/init.d/trafmon

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/trafmon $(1)/etc/config/trafmon
endef

$(eval $(call BuildPackage,trafmon))
