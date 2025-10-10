include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI App for TrafMon (LED Traffic Monitor)
LUCI_DEPENDS:=+luci-compat +luci-base +rpcd +rpcd-mod-uci +rpcd-mod-file +rpcd-mod-iwinfo
LUCI_PKGARCH:=all
PKG_LICENSE:=MIT
PKG_MAINTAINER:=Najahi

include $(TOPDIR)/feeds/luci/luci.mk

define Package/luci-app-trafmon/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/controller
	$(INSTALL_BIN) ./luasrc/controller/trafmon.lua $(1)/usr/lib/lua/luci/controller/trafmon.lua

	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/trafmon
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/trafmon/config.js \
		$(1)/www/luci-static/resources/view/trafmon/config.js

	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-trafmon.json \
		$(1)/usr/share/rpcd/acl.d/luci-app-trafmon.json
endef

# Standard LuCI packaging macro
$(eval $(call BuildPackage,luci-app-trafmon))
