#include <iostream>
#include <cstdio>

using namespace std;

int main() {
    string s1,s2,s3;
    int Nread;
    for (int i = 0; i < 8; i++) cin >> s1 >> s2 >> s3;
    cin >> s1 >> s2 >> Nread;
    cout << "Number of Update Operations, " << Nread << endl;
    for (int ops, sum = 0, i = 0; i < Nread; i++) {
        cin >> s1 >> s2 >> ops;
        sum += ops;
        if (i % 5 == 0) cout << (sum * 100) / (double)Nread << endl;
        if (i > 50) break;
    }
    while (cin >> s1 >> s2 >> s3) {
        if (s1[1]=='R') break;
    }
    for (int i = 0; i < 5; i++) cin >> s1 >> s2 >> s3;
    cin >> s1 >> s2 >> Nread;
    cout << endl;
    cout << "Reead Operations, " << Nread << endl;
    for (int ops, sum = 0, i = 0; i < Nread; i++) {
        cin >> s1 >> s2 >> ops;
        sum += ops;
        if (i % 5 == 0) cout << (sum * 100) / (double)Nread << endl;
        if (i > 50) break;
    }

}
