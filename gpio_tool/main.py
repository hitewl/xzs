from PySide6.QtGui import *
from PySide6.QtCore import *
from PySide6.QtWidgets import *
import sys
from qtawesome import icon
from Ui_gpio import Ui_MainWindow
from gpio_data import plat_config
class MainWindow(QMainWindow, Ui_MainWindow,plat_config):
    def __init__(self):
        super().__init__()
        plat_config.__init__(self)
        self.setupUi(self)
        self.setWindowIcon(icon("fa.opera", color="green"))
        self.button_search.clicked.connect(self.hander_button_search)
        self.combox.currentIndexChanged.connect(self.hander_combox_change_event)
        self.combox.setCurrentIndex(0)
        self.clear_button.clicked.connect(self.hander_clear_button)
        self.actionsave.setShortcut("Ctrl+s")
        self.actionsave.setStatusTip("Click to call the function")
        self.actionsave.triggered.connect(self.hander_actionsave_event)
    def hander_actionsave_event(self):
        print("action save")
        
    def hander_button_search(self):
        self.input_text = self.lineEdit.text()
        self.gpio_data = self.config_data[self.select_plat]["gpio_data"]
        self.gpio_direct =self.config_data[self.select_plat]["gpio_dir"]
        for key, key_value in self.gpio_data.items():
            if key == self.input_text:
                self.textshowinfo.append(f"{key_value}")
                break

        for key, key_value in self.gpio_direct.items():
            if key == self.input_text:
                self.textshowinfo.append(f"{key_value}")
                break
            
    def hander_combox_change_event(self, index):
        if index == 0:
            self.select_plat = "N10"
            print(self.select_plat)
        elif index == 1:
            self.select_plat = "N8"
            print(self.select_plat)
        elif index == 2:
            self.select_plat = "N4"
            print(self.select_plat)
    def hander_clear_button(self):
        self.textshowinfo.clear()
        self.update()
        
if __name__ == "__main__":

    app = QApplication([])
    window = MainWindow()
    window.setWindowTitle("gpio_tool")
    window.show()
    app.exec()
