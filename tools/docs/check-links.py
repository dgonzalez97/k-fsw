#!/usr/bin/env python3
"""Check internal links in the printable guide before rendering it."""

import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Guide(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []
        self.errors = []

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        identifier = attrs.get("id")
        if identifier:
            if identifier in self.ids:
                self.errors.append(f"duplicate ID: {identifier}")
            self.ids.add(identifier)
        if tag == "a" and "href" in attrs:
            self.links.append(attrs["href"])

    def check(self):
        for target in self.links:
            url = urlsplit(target)
            if url.scheme or url.netloc:
                continue
            if url.path:
                self.errors.append(f"unresolved local link: {target}")
            elif url.fragment and unquote(url.fragment) not in self.ids:
                self.errors.append(f"missing anchor: {target}")
        return self.errors


if __name__ == "__main__":
    guide = Guide()
    guide.feed(Path(sys.argv[1]).read_text(encoding="utf-8"))
    errors = guide.check()
    for error in errors:
        print(f"PDF: {error}", file=sys.stderr)
    sys.exit(bool(errors))
