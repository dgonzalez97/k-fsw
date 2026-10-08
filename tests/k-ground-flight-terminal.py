#!/usr/bin/env python3
"""Interactive k-ground shell linked to hosted flight over PTY KISS."""
import tempfile
from pathlib import Path
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent / 'hil/resources'))
from Ground import Ground

with tempfile.TemporaryDirectory(prefix='k-ground-flight-') as output:
    pair = Ground()
    try:
        pair.open_ground_pair(str(Path(output) / 'run'))
        offset = len(pair.ground.log.read_text())
        stopped = threading.Event()

        def relay():
            global offset
            while not stopped.wait(.05):
                text = pair.ground.log.read_text()
                print(text[offset:], end='', flush=True)
                offset = len(text)

        worker = threading.Thread(target=relay)
        worker.start()
        print('K-GROUND FLIGHT TERMINAL: READY', flush=True)
        try:
            for command in sys.stdin:
                pair.ground.send(command.rstrip('\n'))
        finally:
            stopped.set()
            worker.join()
    finally:
        pair.close_ground_pair()
