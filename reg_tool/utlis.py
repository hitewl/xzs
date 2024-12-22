import os 
def mystartwith(s, prefix, start=0, end=0)->bool:
    if start < 0 or start >= end or (len(prefix)> end-start):
        return False
    return s[start:start+len(prefix)] == prefix

def hex_to_binary(hex_string)->str:
    num = int(hex_string, 16)
    binary_string = bin(num)[2:]
    return binary_string
def decimal_to_binary(decimal_sting)->str:
   
    num = int(decimal_sting, 10)
    binary_string = bin(num)[2:]
    return binary_string
    