import init

import_func = init._import

def testfiles(filenamelist):
    suc = False
    for filename in filenamelist:
        try:
            g = {}
            print("=" * 50)
            print(filename)
            print("=" * 50)
            import_func(g, filename)
        except Exception as e:
            traceback()
            print("test failed -- " + filename)
            return
    print("all tests passed!!!")
    
testfiles(
    [
        # test base object
        "test-string",
        "test-list",
        
        # test expression
        "test-op",
        "test-for",
        "test-function",
        "test-while",
    ]
)