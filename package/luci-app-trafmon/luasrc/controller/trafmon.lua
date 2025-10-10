module("luci.controller.trafmon", package.seeall)

function index()
	if not nixio.fs.access("/etc/config/trafmon") then
		return
	end

	entry({"admin", "system", "trafmon"},
		firstchild(), _("TrafMon"), 60).dependent = false

	entry({"admin", "system", "trafmon", "config"},
		view("trafmon/config"), _("Configuration"), 1).leaf = true
end
