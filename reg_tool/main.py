from PySide6.QtWidgets import *
from PySide6.QtGui import *
from PySide6.QtCore import * 
from Ui_reg_tool import Ui_MainWindow
import sys
from utlis import *

class MainWindow(QMainWindow, Ui_MainWindow):
    def __init__(self):
        super().__init__()
        self.reg_tool = Ui_MainWindow()
        self.reg_tool.setupUi(self)
        self.line_input = self.reg_tool.lineEdit
        self.line_input.textChanged.connect(self.hander_textChanged) 
        self.spinBox_list = []
        self.reg_tool.button_hex.clicked.connect(self.hander_button_hex_checked)
        self.reg_tool.button_dec.clicked.connect(self.hander_button_dec_checked)
        self.reg_tool.button_oct.clicked.connect(self.hander_button_oct_checked)
        for i in range(16):
            self.buttom_name ="pushButton_"+str(i)
            #通过属性名获取对应，加入链表中
            self.spinBox_list.append(getattr(self.reg_tool, self.buttom_name))   
    #当输入框有文本输入时，更新按钮控件内容
    def hander_textChanged(self):
        self.input_text = self.line_input.text()
        print(self.input_text)
        if self.input_text:
            print(f"he {self.input_text}")
            if self.input_text.startswith("0x") or self.input_text.startswith("0X"): 
                print(f"{self.input_text}")
                self.input_text = hex_to_binary(self.input_text)
                print(f"22{self.input_text}")
            else:
                print(f"ta {self.input_text}")
                self.input_text = decimal_to_binary(self.input_text)
                print(f"ni {self.input_text}")
            print(self.input_text)
            if self.input_text:
                for i in range(len(self.input_text)+1):
                    if i >= len(self.spinBox_list):
                        print("input data greater than list length")
                        break
                    self.spinBox_list[i].setText(self.input_text[i])       
        self.reg_tool.centralwidget.update()
    def hander_button_hex_checked(self):
        print("button_hex")
    def hander_button_dec_checked(self):
        print("button_dec")
    def hander_button_oct_checked(self):
        print("button_oct")
if __name__ == '__main__':
    app = QApplication()
    w = MainWindow()
    w.show()
    app.exec()

