#include <iostream>
#include <string>
#include <list>

using namespace std;

int main()
{
    list<string> ss;

    for(int i=0; i<10; i++) {
        char buf[32];
        sprintf(buf, "%d ", i);
        ss.push_back(string(buf));
    }

    list<string>::iterator iter;
    for (iter = ss.begin(); iter != ss.end(); iter ++) {
        cout << *iter;
    }
    cout << endl;

    return 0;
}
