from __future__ import annotations

import json
import traceback
from pathlib import Path

from .decisions import Decision, apply_decisions, load_decisions, make_decision, save_decisions
from .engine import ScanConfig, ScanResult, run_scan
from .models import Conflict
from .patch_writer import write_patch_mod

try:
    from PySide6.QtCore import QObject, Qt, QThread, Signal, Slot
    from PySide6.QtWidgets import (
        QApplication,
        QCheckBox,
        QComboBox,
        QFileDialog,
        QGridLayout,
        QGroupBox,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QMainWindow,
        QMessageBox,
        QPushButton,
        QSplitter,
        QTableWidget,
        QTableWidgetItem,
        QTextEdit,
        QVBoxLayout,
        QWidget,
    )
except Exception as exc:  # noqa: BLE001
    raise RuntimeError(
        "PySide6 is required for GUI mode. Install with: pip install pyside6"
    ) from exc


class ScanWorker(QObject):
    finished = Signal(object)
    failed = Signal(str)

    def __init__(self, config: ScanConfig) -> None:
        super().__init__()
        self.config = config

    @Slot()
    def run(self) -> None:
        try:
            result = run_scan(self.config, write_output_reports=True)
            self.finished.emit(result)
        except Exception:  # noqa: BLE001
            self.failed.emit(traceback.format_exc())


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("LP Conflict Resolver")
        self.resize(1500, 950)

        self.scan_result: ScanResult | None = None
        self.decisions: dict[str, Decision] = {}
        self._conflict_by_nif: dict[str, Conflict] = {}
        self._worker_thread: QThread | None = None
        self._worker: ScanWorker | None = None

        self._build_ui()

    def _build_ui(self) -> None:
        root = QWidget(self)
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(8)

        controls = self._build_controls_group()
        layout.addWidget(controls)

        self.summary_label = QLabel("No scan run yet.")
        layout.addWidget(self.summary_label)

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self._build_conflicts_panel())
        splitter.addWidget(self._build_details_panel())
        splitter.setSizes([900, 600])
        layout.addWidget(splitter)

    def _build_controls_group(self) -> QWidget:
        group = QGroupBox("Scan And Output")
        grid = QGridLayout(group)

        self.mo2_root_edit = QLineEdit("C:\\Modlists")
        self.profile_path_edit = QLineEdit("C:\\Modlists\\profiles\\Borealis")
        self.output_dir_edit = QLineEdit(str(Path.home() / "Documents" / "LPConflictResolver"))
        self.patch_name_edit = QLineEdit("LP_ConflictPatch")

        self.pl_source_combo = QComboBox()
        self.pl_source_combo.addItem("NIF (ENB Particle Lights)", "nif")
        self.pl_source_combo.addItem("JSON", "json")
        self.pl_source_combo.addItem("Both", "both")

        self.only_overlap_cb = QCheckBox("Only overlap")
        self.ignore_duplicate_exact_cb = QCheckBox("Ignore duplicate_exact")
        self.cross_mod_duplicates_cb = QCheckBox("Cross-mod duplicates only")

        browse_mo2_btn = QPushButton("Browse")
        browse_profile_btn = QPushButton("Browse")
        browse_output_btn = QPushButton("Browse")
        browse_mo2_btn.clicked.connect(lambda: self._browse_directory(self.mo2_root_edit))
        browse_profile_btn.clicked.connect(lambda: self._browse_directory(self.profile_path_edit))
        browse_output_btn.clicked.connect(lambda: self._browse_directory(self.output_dir_edit))

        self.scan_btn = QPushButton("Scan")
        self.scan_btn.clicked.connect(self.start_scan)

        load_decisions_btn = QPushButton("Load Decisions")
        save_decisions_btn = QPushButton("Save Decisions")
        export_patch_btn = QPushButton("Export Patch")
        load_decisions_btn.clicked.connect(self.load_decisions_from_disk)
        save_decisions_btn.clicked.connect(self.save_decisions_to_disk)
        export_patch_btn.clicked.connect(self.export_patch_mod)

        apply_overlap_disable_btn = QPushButton("Disable LP For All Overlaps")
        apply_overlap_disable_btn.clicked.connect(self.apply_disable_for_all_overlaps)
        apply_highest_duplicates_btn = QPushButton("Keep Highest For All Duplicates")
        apply_highest_duplicates_btn.clicked.connect(self.apply_keep_highest_for_all_duplicates)

        grid.addWidget(QLabel("MO2 Root"), 0, 0)
        grid.addWidget(self.mo2_root_edit, 0, 1)
        grid.addWidget(browse_mo2_btn, 0, 2)
        grid.addWidget(QLabel("Profile Path"), 1, 0)
        grid.addWidget(self.profile_path_edit, 1, 1)
        grid.addWidget(browse_profile_btn, 1, 2)
        grid.addWidget(QLabel("Output Dir"), 2, 0)
        grid.addWidget(self.output_dir_edit, 2, 1)
        grid.addWidget(browse_output_btn, 2, 2)
        grid.addWidget(QLabel("Patch Mod Name"), 3, 0)
        grid.addWidget(self.patch_name_edit, 3, 1)
        grid.addWidget(QLabel("PL Source"), 4, 0)
        grid.addWidget(self.pl_source_combo, 4, 1)

        filter_row = QHBoxLayout()
        filter_row.addWidget(self.only_overlap_cb)
        filter_row.addWidget(self.ignore_duplicate_exact_cb)
        filter_row.addWidget(self.cross_mod_duplicates_cb)
        filter_row.addStretch(1)
        grid.addLayout(filter_row, 5, 0, 1, 3)

        button_row = QHBoxLayout()
        button_row.addWidget(self.scan_btn)
        button_row.addWidget(load_decisions_btn)
        button_row.addWidget(save_decisions_btn)
        button_row.addWidget(export_patch_btn)
        button_row.addWidget(apply_overlap_disable_btn)
        button_row.addWidget(apply_highest_duplicates_btn)
        button_row.addStretch(1)
        grid.addLayout(button_row, 6, 0, 1, 3)

        return group

    def _build_conflicts_panel(self) -> QWidget:
        box = QGroupBox("Conflicts")
        layout = QVBoxLayout(box)

        self.conflicts_table = QTableWidget(0, 6)
        self.conflicts_table.setHorizontalHeaderLabels(["NIF", "Types", "LP Mods", "LP #", "PL #", "Decision"])
        self.conflicts_table.setSelectionBehavior(QTableWidget.SelectRows)
        self.conflicts_table.setSelectionMode(QTableWidget.SingleSelection)
        self.conflicts_table.itemSelectionChanged.connect(self.on_conflict_selection_changed)
        self.conflicts_table.setSortingEnabled(True)

        layout.addWidget(self.conflicts_table)
        return box

    def _build_details_panel(self) -> QWidget:
        box = QGroupBox("Details And Decisions")
        layout = QVBoxLayout(box)

        decision_row = QHBoxLayout()
        self.action_combo = QComboBox()
        self.action_combo.addItem("Ignore", "ignore")
        self.action_combo.addItem("Keep Highest Priority LP", "keep_highest_priority")
        self.action_combo.addItem("Choose Specific LP Entry", "choose_entry")
        self.action_combo.addItem("Disable LP", "disable_lp")
        self.entry_combo = QComboBox()
        self.entry_combo.setMinimumWidth(260)
        apply_btn = QPushButton("Apply To Selected")
        clear_btn = QPushButton("Clear Decision")
        apply_btn.clicked.connect(self.apply_decision_to_selected)
        clear_btn.clicked.connect(self.clear_decision_for_selected)
        decision_row.addWidget(QLabel("Action"))
        decision_row.addWidget(self.action_combo)
        decision_row.addWidget(QLabel("LP Entry"))
        decision_row.addWidget(self.entry_combo)
        decision_row.addWidget(apply_btn)
        decision_row.addWidget(clear_btn)
        decision_row.addStretch(1)
        layout.addLayout(decision_row)

        self.detail_text = QTextEdit()
        self.detail_text.setReadOnly(True)
        layout.addWidget(self.detail_text)
        return box

    def _browse_directory(self, target_edit: QLineEdit) -> None:
        initial = target_edit.text().strip() or "."
        selected = QFileDialog.getExistingDirectory(self, "Select Directory", initial)
        if selected:
            target_edit.setText(selected)

    def _build_scan_config(self) -> ScanConfig:
        return ScanConfig(
            mo2_root=Path(self.mo2_root_edit.text().strip()),
            profile_path=Path(self.profile_path_edit.text().strip()),
            output_dir=Path(self.output_dir_edit.text().strip()),
            pl_source=self.pl_source_combo.currentData(),
            only_overlap=self.only_overlap_cb.isChecked(),
            ignore_duplicate_exact=self.ignore_duplicate_exact_cb.isChecked(),
            cross_mod_lp_duplicates_only=self.cross_mod_duplicates_cb.isChecked(),
        )

    def start_scan(self) -> None:
        self.scan_btn.setEnabled(False)
        self.summary_label.setText("Scanning...")
        self.detail_text.setPlainText("")
        self.conflicts_table.setRowCount(0)
        self.entry_combo.clear()

        config = self._build_scan_config()
        worker = ScanWorker(config)
        thread = QThread(self)
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        worker.finished.connect(self.on_scan_finished)
        worker.failed.connect(self.on_scan_failed)
        worker.finished.connect(thread.quit)
        worker.failed.connect(thread.quit)
        worker.finished.connect(worker.deleteLater)
        worker.failed.connect(worker.deleteLater)
        thread.finished.connect(thread.deleteLater)

        self._worker_thread = thread
        self._worker = worker
        thread.start()

    @Slot(object)
    def on_scan_finished(self, result: object) -> None:
        self.scan_btn.setEnabled(True)
        self._worker = None
        self._worker_thread = None
        if not isinstance(result, ScanResult):
            self.summary_label.setText("Scan failed: invalid result type")
            return

        self.scan_result = result
        self.decisions = {}
        self._load_default_decisions_if_present()

        self._populate_conflicts_table()
        self.summary_label.setText(
            "Enabled mods: {0} | LP files: {1} | PL candidates: {2} | LP entries: {3} | PL targets: {4} | "
            "Conflicts(raw/filtered): {5}/{6}".format(
                result.enabled_mod_count,
                result.lp_candidate_files,
                result.pl_candidate_files,
                len(result.lp_entries),
                len(result.pl_targets),
                len(result.detected_conflicts),
                len(result.conflicts),
            )
        )

    @Slot(str)
    def on_scan_failed(self, error_text: str) -> None:
        self.scan_btn.setEnabled(True)
        self._worker = None
        self._worker_thread = None
        self.summary_label.setText("Scan failed.")
        QMessageBox.critical(self, "Scan Failed", error_text)

    def _populate_conflicts_table(self) -> None:
        self.conflicts_table.setSortingEnabled(False)
        self.conflicts_table.setRowCount(0)
        self._conflict_by_nif = {}
        if self.scan_result is None:
            return

        for row, conflict in enumerate(self.scan_result.conflicts):
            self._conflict_by_nif[conflict.nif_path_canonical] = conflict
            self.conflicts_table.insertRow(row)
            lp_mods = sorted({entry.source_mod for entry in conflict.lp_entries})
            decision = self.decisions.get(conflict.nif_path_canonical)
            decision_label = decision.action if decision else ""
            values = [
                conflict.nif_path_canonical,
                ", ".join(conflict.conflict_types),
                ", ".join(lp_mods),
                str(len(conflict.lp_entries)),
                str(len(conflict.pl_targets)),
                decision_label,
            ]
            for col, value in enumerate(values):
                item = QTableWidgetItem(value)
                if col in {3, 4}:
                    item.setTextAlignment(Qt.AlignCenter)
                if col == 0:
                    item.setData(Qt.UserRole, conflict.nif_path_canonical)
                self.conflicts_table.setItem(row, col, item)
        self.conflicts_table.setSortingEnabled(True)
        self.conflicts_table.resizeColumnsToContents()

    def _selected_conflict(self) -> Conflict | None:
        selected = self.conflicts_table.selectedItems()
        if not selected:
            return None
        row = selected[0].row()
        nif_item = self.conflicts_table.item(row, 0)
        if nif_item is None:
            return None
        nif_path = nif_item.data(Qt.UserRole)
        if not isinstance(nif_path, str):
            return None
        return self._conflict_by_nif.get(nif_path)

    def on_conflict_selection_changed(self) -> None:
        conflict = self._selected_conflict()
        self.entry_combo.clear()
        if conflict is None:
            self.detail_text.setPlainText("")
            return

        for entry in conflict.lp_entries:
            label = f"{entry.source_mod} | prio {entry.source_priority} | {entry.source_file} | {entry.entry_id[:10]}"
            self.entry_combo.addItem(label, entry.entry_id)

        decision = self.decisions.get(conflict.nif_path_canonical)
        if decision is not None:
            index = self.action_combo.findData(decision.action)
            if index >= 0:
                self.action_combo.setCurrentIndex(index)
            if decision.entry_id:
                entry_index = self.entry_combo.findData(decision.entry_id)
                if entry_index >= 0:
                    self.entry_combo.setCurrentIndex(entry_index)

        detail = self._render_conflict_detail(conflict)
        self.detail_text.setPlainText(detail)

    def _render_conflict_detail(self, conflict: Conflict) -> str:
        lines = [
            f"NIF: {conflict.nif_path_canonical}",
            f"Types: {', '.join(conflict.conflict_types)}",
            f"LP entries: {len(conflict.lp_entries)} | PL targets: {len(conflict.pl_targets)}",
            "",
            "LP Entries:",
        ]
        for entry in conflict.lp_entries:
            lines.append(f"- {entry.source_mod} (prio {entry.source_priority}) {entry.source_file}")
            lines.append(f"  entry_id: {entry.entry_id}")
            lines.append(f"  settings: {json.dumps(entry.settings, ensure_ascii=True)}")
        lines.append("")
        lines.append("PL Targets:")
        for target in conflict.pl_targets:
            lines.append(f"- {target.source_mod} (prio {target.source_priority}) {target.source_file}")
        return "\n".join(lines)

    def apply_decision_to_selected(self) -> None:
        conflict = self._selected_conflict()
        if conflict is None:
            return

        action = self.action_combo.currentData()
        entry_id = None
        if action == "choose_entry":
            entry_id = self.entry_combo.currentData()
            if not entry_id:
                QMessageBox.warning(self, "Decision", "Choose LP entry for 'Choose Specific LP Entry'.")
                return

        self.decisions[conflict.nif_path_canonical] = make_decision(action=action, entry_id=entry_id)
        self._populate_conflicts_table()
        self.on_conflict_selection_changed()

    def clear_decision_for_selected(self) -> None:
        conflict = self._selected_conflict()
        if conflict is None:
            return
        self.decisions.pop(conflict.nif_path_canonical, None)
        self._populate_conflicts_table()
        self.on_conflict_selection_changed()

    def apply_disable_for_all_overlaps(self) -> None:
        if self.scan_result is None:
            return
        for conflict in self.scan_result.conflicts:
            if "lp_vs_pl_overlap" in conflict.conflict_types:
                self.decisions[conflict.nif_path_canonical] = make_decision(action="disable_lp")
        self._populate_conflicts_table()
        QMessageBox.information(self, "Decisions", "Applied disable_lp to all overlap conflicts.")

    def apply_keep_highest_for_all_duplicates(self) -> None:
        if self.scan_result is None:
            return
        for conflict in self.scan_result.conflicts:
            if "duplicate_exact" in conflict.conflict_types or "duplicate_divergent" in conflict.conflict_types:
                self.decisions[conflict.nif_path_canonical] = make_decision(action="keep_highest_priority")
        self._populate_conflicts_table()
        QMessageBox.information(self, "Decisions", "Applied keep_highest_priority to all duplicate conflicts.")

    def _default_decisions_path(self) -> Path:
        return Path(self.output_dir_edit.text().strip()) / "resolver_decisions.json"

    def _load_default_decisions_if_present(self) -> None:
        if self.scan_result is None:
            return
        decisions_path = self._default_decisions_path()
        if not decisions_path.exists():
            return
        decisions = load_decisions(decisions_path)
        applied, stale = apply_decisions(self.scan_result.conflicts, decisions)
        self.decisions = applied
        if stale:
            self.summary_label.setText(self.summary_label.text() + f" | Stale decisions skipped: {len(stale)}")

    def load_decisions_from_disk(self) -> None:
        if self.scan_result is None:
            QMessageBox.warning(self, "Load Decisions", "Run scan first.")
            return
        default_path = str(self._default_decisions_path())
        selected_path, _ = QFileDialog.getOpenFileName(
            self,
            "Load Decisions",
            default_path,
            "JSON Files (*.json);;All Files (*.*)",
        )
        if not selected_path:
            return

        decisions = load_decisions(Path(selected_path))
        applied, stale = apply_decisions(self.scan_result.conflicts, decisions)
        self.decisions = applied
        self._populate_conflicts_table()
        message = f"Loaded decisions: {len(applied)}"
        if stale:
            message += f" | Stale skipped: {len(stale)}"
        QMessageBox.information(self, "Load Decisions", message)

    def save_decisions_to_disk(self) -> None:
        default_path = str(self._default_decisions_path())
        selected_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save Decisions",
            default_path,
            "JSON Files (*.json);;All Files (*.*)",
        )
        if not selected_path:
            return
        save_decisions(Path(selected_path), self.decisions)
        QMessageBox.information(self, "Save Decisions", f"Saved {len(self.decisions)} decisions.")

    def export_patch_mod(self) -> None:
        if self.scan_result is None:
            QMessageBox.warning(self, "Export Patch", "Run scan first.")
            return
        patch_name = self.patch_name_edit.text().strip() or "LP_ConflictPatch"
        result = write_patch_mod(self.scan_result, self.decisions, patch_mod_name=patch_name)
        message = (
            f"Patch written to:\n{result.patch_mod_dir}\n\n"
            f"Selected NIF decisions: {result.selected_nif_count}\n"
            f"Exported LP entries: {result.selected_entry_count}\n"
            f"Warnings: {len(result.warnings)}"
        )
        QMessageBox.information(self, "Export Patch", message)


def main() -> int:
    app = QApplication([])
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
