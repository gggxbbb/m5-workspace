"""liangzi-meter 上位机入口。"""
from __future__ import annotations

import sys

from PyQt6.QtWidgets import QApplication

from .ui import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("liangzi-meter")
    win = MainWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
