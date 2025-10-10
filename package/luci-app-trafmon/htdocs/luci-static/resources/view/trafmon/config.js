"use strict";
"require view";
"require ui";
"require uci";
"require form";
"require fs";
"require network";
"require rpc";

var callInitAction = rpc.declare({
  object: "luci",
  method: "setInitAction",
  params: ["name", "action"],
  expect: { result: false },
});

function listRunningInstances() {
  return fs
    .list("/var/run")
    .then((entries) => {
      var out = [];
      for (var i = 0; i < entries.length; i++) {
        var e = entries[i];
        if (!e || !e.name || e.type !== "file") continue;
        if (!e.name.match(/^trafmon_.+\.iface$/)) continue;

        out.push(e.name);
      }
      return Promise.all(
        out.map((name) =>
          fs
            .readfile("/var/run/" + name)
            .then((txt) => {
              var m = String(txt || "")
                .trim()
                .split(/\s+/);
              return { file: name, iface: m[0] || "?", led: m[1] || "?" };
            })
            .catch(() => ({ file: name, iface: "?", led: "?" }))
        )
      );
    })
    .catch(() => []);
}

return view.extend({
  load: function () {
    return Promise.all([
      uci.load("trafmon"),
      network.init(),
      listRunningInstances(),
    ]);
  },

  render: function (loadres) {
    var running = loadres[2] || [];

    var m = new form.Map(
      "trafmon",
      _("TrafMon"),
      _(
        "LED traffic monitor daemon. Configure instances here; service will be restarted on Save & Apply."
      )
    );

    var s = m.section(form.TypedSection, "instance", _("Instances"));
    s.addremove = true;
    s.anonymous = false;
    s.nodescriptions = true;

    var o;

    o = s.option(form.Flag, "enabled", _("Enabled"));
    o.default = o.disabled;
    o.rmempty = false;

    o = s.option(form.ListValue, "ifname", _("Interface"));
    o.datatype = "string";
    o.rmempty = false;

    // Populate interfaces dynamically
    var devs = network.getDevices() || [];
    devs.forEach(function (d) {
      // Show logical ifnames (e.g. br-lan, eth0, wan, wlan0)
      if (d && d.getName) o.value(d.getName());
    });

    // LEDs
    o = s.option(form.ListValue, "led", _("LED"));
    o.value("lan", "lan");
    o.value("power", "power");
    o.default = "lan";
    o.rmempty = false;

    /* Running instances panel */
    var stat = m.section(form.TypedSection, "_runtime", _("Running instances"));
    stat.anonymous = true;
    stat.render = function () {
      var table = E(
        "table",
        { class: "table" },
        E(
          "tr",
          {},
          E("th", {}, _("Lock File")),
          E("th", {}, _("Interface")),
          E("th", {}, _("LED"))
        )
      );

      if (!running.length) {
        table.appendChild(
          E(
            "tr",
            {},
            E("td", { colspan: 3 }, _("No running trafmon instances."))
          )
        );
      } else {
        running.forEach(function (item) {
          table.appendChild(
            E(
              "tr",
              {},
              E("td", {}, item.file),
              E("td", {}, item.iface),
              E("td", {}, item.led)
            )
          );
        });
      }

      return E("div", { class: "cbi-section" }, table);
    };

    /* Save & Apply → restart service */
    m.handleSaveApply = function (ev, mode) {
      return this.save()
        .then(
          L.bind(function () {
            return uci.apply();
          }, this)
        )
        .then(function () {
          // restart daemon after apply
          return callInitAction("trafmon", "restart");
        })
        .then(function () {
          ui.addNotification(null, E("p", {}, _("TrafMon service restarted.")));
          // Refresh page to re-pull running instances
          window.location.reload();
        })
        .catch(function (err) {
          ui.addNotification(_("Failed"), E("p", {}, err.message || err));
        });
    };

    return m.render();
  },
});
