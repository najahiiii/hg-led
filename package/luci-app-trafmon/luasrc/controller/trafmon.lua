module("luci.controller.trafmon", package.seeall)

function index()
	if not nixio.fs.access("/etc/config/trafmon") then
		return
	end

	entry({"admin", "services", "trafmon"},
		firstchild(), _("TrafMon"), 60).dependent = false

	entry({"admin", "services", "trafmon", "config"},
		view("trafmon/config"), _("Configuration"), 1).leaf = true
end
