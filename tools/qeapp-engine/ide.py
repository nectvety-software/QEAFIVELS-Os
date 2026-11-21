#!/usr/bin/env python3
"""Minimal QEAPP-Engine IDE (tkinter fallback; PySide6 not installed)."""
import subprocess
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, scrolledtext

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "sdk" / "cli" / "qeaf.py"

class IDE(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("QEAPP-Engine IDE")
        self.geometry("900x600")
        self.project = tk.StringVar()

        top = tk.Frame(self)
        top.pack(fill=tk.X, padx=8, pady=8)
        tk.Label(top, text="Project:").pack(side=tk.LEFT)
        tk.Entry(top, textvariable=self.project, width=60).pack(side=tk.LEFT, padx=4)
        tk.Button(top, text="Browse", command=self.browse).pack(side=tk.LEFT)
        tk.Button(top, text="Create", command=self.create).pack(side=tk.LEFT, padx=4)
        tk.Button(top, text="Build", command=self.build).pack(side=tk.LEFT, padx=4)

        self.editor = scrolledtext.ScrolledText(self, height=20)
        self.editor.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        self.log = scrolledtext.ScrolledText(self, height=10)
        self.log.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

    def browse(self):
        d = filedialog.askdirectory()
        if d:
            self.project.set(d)
            main = Path(d) / "src" / "main.qe"
            if main.is_file():
                self.editor.delete("1.0", tk.END)
                self.editor.insert("1.0", main.read_text(encoding="utf-8"))

    def run(self, args):
        self.log.delete("1.0", tk.END)
        proc = subprocess.run([sys.executable, str(SDK)] + args,
                              capture_output=True, text=True, encoding="utf-8")
        self.log.insert(tk.END, proc.stdout)
        if proc.stderr:
            self.log.insert(tk.END, "\nSTDERR:\n" + proc.stderr)
        if proc.returncode != 0:
            messagebox.showerror("QEAPP-Engine", "Command failed")

    def create(self):
        name = self.project.get().strip()
        if not name:
            messagebox.showerror("QEAPP-Engine", "Project path required")
            return
        self.run(["create", name])

    def build(self):
        name = self.project.get().strip()
        if not name:
            messagebox.showerror("QEAPP-Engine", "Project path required")
            return
        # save editor back to src/main.qe
        main = Path(name) / "src" / "main.qe"
        if main.parent.exists():
            main.write_text(self.editor.get("1.0", tk.END), encoding="utf-8")
        self.run(["build", name, "--config", "all"])

if __name__ == "__main__":
    IDE().mainloop()
