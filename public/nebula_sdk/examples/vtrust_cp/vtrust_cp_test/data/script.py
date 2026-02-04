import vtrust
print('Micropython script start')
s=vtrust.cp.get_data_size()
print(s)
d=vtrust.cp.get_data()
print(d)
vtrust.cp.set_res_data('test response data', 18)
print('Micropython script finish')
