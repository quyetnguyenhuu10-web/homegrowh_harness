/* Edit stress report app: theme, tooltips, table tools, collapse. */
(function () {
    "use strict";

    var root = document.documentElement;

    /* ---------------------------------- theme ---------------------------------- */

    var THEME_KEY = "edit-report-theme";
    var THEME_ORDER = ["auto", "light", "dark"];
    var THEME_LABEL = {
        auto: "Auto",
        light: "Light",
        dark: "Dark",
    };

    function currentTheme() {
        try {
            var saved = window.localStorage.getItem(THEME_KEY);
            return THEME_ORDER.indexOf(saved) === -1 ? "auto" : saved;
        } catch (error) {
            return "auto";
        }
    }

    function applyTheme(theme) {
        root.setAttribute("data-theme", theme);

        var button = document.querySelector("[data-theme-toggle]");

        if (button) {
            button.setAttribute(
                "data-active-theme",
                theme
            );
            button.setAttribute(
                "aria-label",
                "Theme: " + THEME_LABEL[theme] + " (click to change)"
            );

            var label = button.querySelector("[data-theme-label]");

            if (label) {
                label.textContent = THEME_LABEL[theme];
            }
        }
    }

    function initTheme() {
        applyTheme(currentTheme());

        document.addEventListener("click", function (event) {
            var button = event.target.closest("[data-theme-toggle]");

            if (!button) {
                return;
            }

            var next =
                THEME_ORDER[
                    (THEME_ORDER.indexOf(currentTheme()) + 1) %
                    THEME_ORDER.length
                ];

            try {
                window.localStorage.setItem(THEME_KEY, next);
            } catch (error) {
                /* Private mode: keep in-memory only. */
            }

            applyTheme(next);
        });
    }

    /* --------------------------------- tooltip --------------------------------- */

    function initTooltip() {
        var tooltip = document.querySelector("[data-tooltip-box]");

        if (!tooltip) {
            return;
        }

        var pinned = null;

        function place(clientX, clientY) {
            var padding = 14;
            var box = tooltip.getBoundingClientRect();

            var left = clientX + padding;
            var top = clientY + padding;

            if (left + box.width > window.innerWidth - 8) {
                left = clientX - box.width - padding;
            }

            if (top + box.height > window.innerHeight - 8) {
                top = clientY - box.height - padding;
            }

            tooltip.style.left = Math.max(8, left) + "px";
            tooltip.style.top = Math.max(8, top) + "px";
        }

        document.addEventListener("mouseover", function (event) {
            if (pinned) {
                return;
            }

            var target = event.target.closest
                ? event.target.closest("[data-tooltip]")
                : null;

            if (!target) {
                return;
            }

            tooltip.textContent = target.getAttribute("data-tooltip");
            tooltip.hidden = false;
            place(event.clientX, event.clientY);
        });

        document.addEventListener("mousemove", function (event) {
            if (tooltip.hidden || pinned) {
                return;
            }

            place(event.clientX, event.clientY);
        });

        function hide() {
            tooltip.hidden = true;
        }

        document.addEventListener("mouseout", function (event) {
            if (pinned) {
                return;
            }

            var target = event.target.closest
                ? event.target.closest("[data-tooltip]")
                : null;

            var next = event.relatedTarget && event.relatedTarget.closest
                ? event.relatedTarget.closest("[data-tooltip]")
                : null;

            if (target && target !== next) {
                hide();
            }
        });

        document.addEventListener("scroll", hide, true);

        document.addEventListener("click", function (event) {
            var target = event.target.closest
                ? event.target.closest("[data-tooltip]")
                : null;

            if (target) {
                pinned = target;
                tooltip.textContent = target.getAttribute("data-tooltip");
                tooltip.hidden = false;
                place(event.clientX, event.clientY);
                return;
            }

            if (event.target.closest &&
                !event.target.closest("[data-tooltip-box]")) {
                pinned = null;
                hide();
            }
        });

        document.addEventListener("keydown", function (event) {
            if (event.key === "Escape" && pinned) {
                pinned = null;
                hide();
            }
        });
    }

    /* ------------------------------- table tools ------------------------------- */

    function initTables() {
        var blocks = Array.prototype.slice.call(
            document.querySelectorAll("[data-table-block]")
        );

        blocks.forEach(function (block) {
            initTableBlock(block);
        });
    }

    function initTableBlock(block) {
        var table = block.querySelector("[data-records-table]");

        if (!table) {
            return;
        }

        var search = block.querySelector("[data-table-search]");
        var chips = Array.prototype.slice.call(
            block.querySelectorAll("[data-outcome-filter]")
        );
        var section = block.closest
            ? block.closest(".result-section")
            : null;
        var count = (section || document).querySelector(
            "[data-visible-count]"
        );
        var total = table.tBodies.length > 0
            ? table.tBodies[0].rows.length
            : 0;
        var state = {
            query: "",
            outcome: "all",
            key: -1,
            direction: 1,
        };

        function applyFilters() {
            var body = table.tBodies[0];

            if (!body) {
                return;
            }

            var visible = 0;

            Array.prototype.forEach.call(body.rows, function (row) {
                var matchesQuery = state.query === "" ||
                    row.textContent.toLowerCase().indexOf(state.query) !== -1;
                var matchesOutcome = state.outcome === "all" ||
                    row.getAttribute("data-outcome") === state.outcome;

                var show = matchesQuery && matchesOutcome;
                row.style.display = show ? "" : "none";

                if (show) {
                    visible += 1;
                }
            });

            if (count) {
                count.textContent = (visible === total)
                    ? total + " records"
                    : visible + " of " + total + " shown";
            }
        }

        if (search) {
            search.addEventListener("input", function () {
                state.query = search.value.trim().toLowerCase();
                applyFilters();
            });
        }

        chips.forEach(function (chip) {
            chip.addEventListener("click", function () {
                state.outcome = chip.getAttribute("data-outcome-filter");

                chips.forEach(function (other) {
                    var active = other === chip;
                    other.classList.toggle("is-active", active);
                    other.setAttribute("aria-pressed", active ? "true" : "false");
                });

                applyFilters();
            });
        });

        function cellValue(row, index, numeric) {
            var text = row.cells[index].textContent.trim();

            if (!numeric) {
                return text.toLowerCase();
            }

            var parsed = parseFloat(text.replace(/[^0-9.\-]/g, ""));

            return Number.isFinite(parsed) ? parsed : text.toLowerCase();
        }

        function sortBy(index, numeric, header) {
            var body = table.tBodies[0];

            if (!body) {
                return;
            }

            if (state.key === index) {
                state.direction = -state.direction;
            } else {
                state.key = index;
                state.direction = 1;
            }

            var rows = Array.prototype.slice.call(body.rows);

            rows.sort(function (left, right) {
                var a = cellValue(left, index, numeric);
                var b = cellValue(right, index, numeric);

                if (a < b) {
                    return -state.direction;
                }

                if (a > b) {
                    return state.direction;
                }

                return 0;
            });

            rows.forEach(function (row) {
                body.appendChild(row);
            });

            Array.prototype.forEach.call(
                table.querySelectorAll("th[data-sort]"),
                function (th) {
                    var active = th === header;
                    th.setAttribute("aria-sort", !active
                        ? "none"
                        : (state.direction === 1 ? "ascending" : "descending"));
                    th.classList.toggle("is-sorted", active);
                }
            );
        }

        Array.prototype.forEach.call(
            table.querySelectorAll("th[data-sort]"),
            function (th) {
                th.setAttribute("tabindex", "0");
                th.setAttribute("role", "button");

                function activate() {
                    var cells = Array.prototype.slice.call(th.parentNode.cells);
                    sortBy(
                        cells.indexOf(th),
                        th.getAttribute("data-sort") === "number",
                        th
                    );
                }

                th.addEventListener("click", activate);
                th.addEventListener("keydown", function (event) {
                    if (event.key === "Enter" || event.key === " ") {
                        event.preventDefault();
                        activate();
                    }
                });
            }
        );
    }

    /* ---------------------------------- tabs --------------------------------- */

    function initTabs() {
        var buttons = Array.prototype.slice.call(
            document.querySelectorAll("[data-tab]")
        );

        if (buttons.length === 0) {
            return;
        }

        function activate(id, push) {
            buttons.forEach(function (button) {
                var active = button.getAttribute("data-tab") === id;
                button.classList.toggle("is-active", active);
                button.setAttribute(
                    "aria-selected",
                    active ? "true" : "false"
                );
            });

            Array.prototype.forEach.call(
                document.querySelectorAll("[data-page]"),
                function (page) {
                    page.hidden = page.id !== id;
                }
            );

            if (push !== false) {
                try {
                    window.history.replaceState(null, "", "#" + id);
                } catch (error) {
                    /* file:// or private mode: ignore */
                }
            }

            refitVisibleRamtime();
        }

        buttons.forEach(function (button) {
            button.addEventListener("click", function () {
                activate(button.getAttribute("data-tab"));

                var bar = document.querySelector(".tabbar");

                if (bar && bar.scrollIntoView) {
                    bar.scrollIntoView();
                }
            });
        });

        var initial = window.location.hash.replace("#", "");
        var valid = buttons.some(function (button) {
            return button.getAttribute("data-tab") === initial;
        });

        activate(
            valid ? initial : buttons[0].getAttribute("data-tab"),
            false
        );
    }

    /* -------------------------------- ramtime charts ---------------------------- */

    function ramtimeEscape(value) {
        return String(value)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;");
    }

    function ramtimeFormatMB(bytes) {
        return (Number(bytes) / 1048576).toFixed(2) + " MB";
    }

    function ramtimeFormatSeconds(ms) {
        return (Number(ms) / 1000).toFixed(2) + "s";
    }

    function renderRamPlot(root) {
        var state = root._ramtime;

        if (!state || !(state.slot > 0)) {
            return;
        }

        var data = state.data;
        var cases = data.cases;
        var slot = state.slot;
        var pad = 10;
        var TOP = 34;
        var BOT = 282;
        var W = Math.ceil(pad * 2 + slot * cases.length);
        var topMB = data.topMB > 0 ? data.topMB : 1;

        function valueY(mb) {
            return BOT - mb / topMB * 248;
        }

        var parts = [];

        data.ticksMB.forEach(function (tick) {
            var yy = valueY(tick).toFixed(2);

            parts.push(
                '<line class="grid" x1="' + pad + '" y1="' + yy +
                '" x2="' + (W - pad) + '" y2="' + yy + '"/>'
            );
        });

        parts.push(
            '<line class="axis" x1="' + pad + '" y1="282" x2="' +
            (W - pad) + '" y2="282"/>'
        );

        var labelEvery = Math.max(1, Math.ceil(48 / slot));

        cases.forEach(function (c, i) {
            var cx = pad + slot * (i + 0.5);
            var bw = Math.max(4, Math.min(56, slot * 0.66));
            var x = cx - bw / 2;
            var mb = c.total / 1048576;
            var h = mb / topMB * 248;
            var yy = BOT - h;
            var cls = c.refused ? "series-d" : "series-b";
            var tip = "Edit " + (i + 1) + " · " + c.id + " · " +
                ramtimeFormatMB(c.total) + " · " +
                ramtimeFormatSeconds(c.ms);

            parts.push(
                '<rect class="bar ' + cls + '" x="' + x.toFixed(2) +
                '" y="' + yy.toFixed(2) + '" width="' + bw.toFixed(2) +
                '" height="' + Math.max(1, h).toFixed(2) + '" rx="6" ' +
                'data-tooltip="' + ramtimeEscape(tip) + '"/>'
            );

            if (slot >= 52) {
                parts.push(
                    '<text class="bar-value" x="' + cx.toFixed(2) +
                    '" y="' + (yy - 8).toFixed(2) +
                    '" text-anchor="middle">' +
                    ramtimeEscape(ramtimeFormatMB(c.total)) + '</text>'
                );
            } else {
                parts.push(
                    '<text class="bar-value" x="' + cx.toFixed(2) +
                    '" y="' + (yy - 8).toFixed(2) +
                    '" text-anchor="start" transform="rotate(-45 ' +
                    cx.toFixed(2) + ' ' + (yy - 8).toFixed(2) + ')">' +
                    ramtimeEscape(ramtimeFormatMB(c.total)) + '</text>'
                );
            }

            if (h > 52 && slot >= 12) {
                var midY = yy + h / 2;

                parts.push(
                    '<text class="bar-time" x="' + cx.toFixed(2) +
                    '" y="' + midY.toFixed(2) +
                    '" text-anchor="middle" transform="rotate(-90 ' +
                    cx.toFixed(2) + ' ' + midY.toFixed(2) + ')">' +
                    ramtimeEscape(ramtimeFormatSeconds(c.ms)) + '</text>'
                );
            }

            if (i % labelEvery === 0) {
                parts.push(
                    '<text class="axis-number x-number" x="' +
                    cx.toFixed(2) +
                    '" y="307" text-anchor="middle">Edit ' + (i + 1) +
                    '</text>'
                );
            }
        });

        var plot = root.querySelector("[data-ramtime-plot]");

        if (!plot) {
            return;
        }

        plot.setAttribute("viewBox", "0 0 " + W + " 320");
        plot.style.width = W + "px";
        plot.style.height = "320px";
        plot.innerHTML = parts.join("");

        var card = root.closest ? root.closest("[data-chart-card]") : null;
        var label = card ? card.querySelector("[data-zoom-label]") : null;

        if (label) {
            label.textContent =
                Math.round(slot / state.baseSlot * 100) + "%";
        }
    }

    function refitRamtime(root) {
        var state = root._ramtime;

        if (!state) {
            return;
        }

        var scroll = root.querySelector("[data-ramtime-scroll]");
        var avail = scroll ? scroll.clientWidth : 0;

        if (!(avail > 0)) {
            return;
        }

        var n = state.data.cases.length;
        var slot;

        if (n > 20) {
            slot = avail / 20;
        } else {
            slot = Math.min(120, avail / Math.max(n, 1));
        }

        slot = Math.min(160, Math.max(8, slot));
        state.slot = slot;
        state.baseSlot = slot;
        renderRamPlot(root);
    }

    function refitVisibleRamtime() {
        Array.prototype.forEach.call(
            document.querySelectorAll("[data-ramtime]"),
            function (root) {
                var page = root.closest ? root.closest("[data-page]") : null;

                if (page && page.hidden) {
                    return;
                }

                refitRamtime(root);
            }
        );
    }

    function initRamtime() {
        Array.prototype.forEach.call(
            document.querySelectorAll("[data-ramtime]"),
            function (root) {
                var json = root.querySelector("[data-ramtime-data]");

                if (!json) {
                    return;
                }

                var data;

                try {
                    data = JSON.parse(json.textContent);
                } catch (error) {
                    return;
                }

                root._ramtime = { data: data, slot: 28, baseSlot: 28 };

                var card = root.closest
                    ? root.closest("[data-chart-card]")
                    : null;
                var box = card ? card.querySelector("[data-zoom]") : null;

                if (box) {
                    box.addEventListener("click", function (event) {
                        var btn = event.target.closest
                            ? event.target.closest(
                                "[data-zoom-in],[data-zoom-out],[data-zoom-reset]"
                            )
                            : null;

                        if (!btn) {
                            return;
                        }

                        var st = root._ramtime;

                        if (btn.hasAttribute("data-zoom-in")) {
                            st.slot = Math.min(160, st.slot * 1.25);
                        } else if (btn.hasAttribute("data-zoom-out")) {
                            st.slot = Math.max(8, st.slot / 1.25);
                        } else {
                            st.slot = st.baseSlot;
                        }

                        renderRamPlot(root);
                    });
                }

                renderRamPlot(root);
            }
        );

        refitVisibleRamtime();
    }

    /* --------------------------------- collapse -------------------------------- */

    function initCollapse() {
        document.addEventListener("click", function (event) {
            var button = event.target.closest("[data-collapse]");

            if (!button) {
                return;
            }

            var card = button.closest("[data-chart-card]");

            if (!card) {
                return;
            }

            var collapsed = card.classList.toggle("is-collapsed");
            button.setAttribute("aria-expanded", collapsed ? "false" : "true");
            button.textContent = collapsed ? "+" : "−";
        });
    }

    /* -------------------------------- back to top ------------------------------- */

    function initBackToTop() {
        var button = document.querySelector("[data-back-to-top]");

        if (!button) {
            return;
        }

        function onScroll() {
            button.hidden = window.scrollY < 600;
        }

        window.addEventListener("scroll", onScroll, { passive: true });
        onScroll();

        button.addEventListener("click", function () {
            window.scrollTo({ top: 0, behavior: "smooth" });
        });
    }

    initTheme();
    initTooltip();
    initTables();
    initTabs();
    initRamtime();
    initCollapse();
    initBackToTop();
})();
