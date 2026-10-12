/*
 * The version selector. Every version of the manual is published side by side
 * under its own directory, so switching is a path change: the reader stays on
 * the page they were reading when that page exists in the version they pick.
 */
(function () {
	"use strict";

	/*
	 * The site is published under a path, not at a domain root, so the root
	 * is taken from this script's own URL instead of being assumed. The
	 * script sits beside versions.json and above every version directory.
	 */
	function siteRoot() {
		var element = document.currentScript;

		if (!element) {
			element = document.querySelector('script[src$="kfsw-versions.js"]');
		}
		if (!element) {
			return null;
		}
		return new URL(".", element.src).href;
	}

	function currentSlug(root) {
		var rest = window.location.href.slice(root.length).split("/");
		return rest.length > 1 ? rest[0] : "";
	}

	function pageWithin(root, slug) {
		var rest = window.location.href.slice(root.length).split("/");
		rest.shift();
		return root + slug + "/" + rest.join("/");
	}

	function go(target, fallback) {
		/* A page that one version does not carry would be a 404, so ask
		 * before leaving and fall back to that version's front page. */
		fetch(target, {method: "HEAD"})
			.then(function (response) {
				window.location.href = response.ok ? target : fallback;
			})
			.catch(function () {
				window.location.href = fallback;
			});
	}

	function build(root, manifest) {
		var here = currentSlug(root);
		var box = document.createElement("div");
		var select = document.createElement("select");

		box.className = "kfsw-version-box";
		select.className = "kfsw-version-select";
		select.setAttribute("aria-label", "Documentation version");

		manifest.versions.forEach(function (version) {
			var option = document.createElement("option");
			option.value = version.slug;
			option.textContent = version.label;
			option.selected = version.slug === here;
			select.appendChild(option);
		});

		select.addEventListener("change", function () {
			var slug = select.value;
			go(pageWithin(root, slug), root + slug + "/index.html");
		});

		box.appendChild(select);
		document.body.appendChild(box);
	}

	function start(root) {
		fetch(root + "versions.json")
			.then(function (response) {
				return response.ok ? response.json() : null;
			})
			.then(function (manifest) {
				if (manifest && manifest.versions && manifest.versions.length > 1) {
					build(root, manifest);
				}
			})
			.catch(function () {
				/* Reading one version offline is not worth an error. */
			});
	}

	var root = siteRoot();

	if (root === null) {
		return;
	}
	if (document.readyState === "loading") {
		document.addEventListener("DOMContentLoaded", function () {
			start(root);
		});
	} else {
		start(root);
	}
})();
