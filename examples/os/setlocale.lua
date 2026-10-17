-- os.setlocale([locale [, category]]): sets or queries the C locale.
print(os.setlocale())                         --> C (query the current locale)
print(os.setlocale("C", "numeric"))           --> C
print(os.setlocale("no-such-locale"))         --> nil
