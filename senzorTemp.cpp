/*
   Viitorul suntem noi
   Smart AC

   Pentru BUILD g++ senzorTemp.cpp -o senzorTemp
   Rulare: ./senzorTemp
*/

#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace std;

int main(int argc, char *argv[])
{
    double temp = 23;
    double bias = 0.03;
    int i = 0;
    int lim = 100;
    while(true)
    {
        // Verificam daca e timpul sa inversam bias
        if(i>lim)
        {
            bias = -bias;
            i=0;
        }

        // Genram r numar aleator intre (-1,1)
        double r = ((double) rand() / (RAND_MAX))*2 - 1;
        // Ducem r intre -0.1 si 0.1
        r = r/10;

        temp += r + bias;

        cout<<temp<<endl;

        // -s -S ruleaza silentios, dar afiseaza erorile
        string str = "curl -s -S http://localhost:9080/senzor/temperatura/"+ to_string(temp) +" > outSenzorTemp.txt";

        const char *command = str.c_str();
        system(command);

        // Verificare rulare cu succes
        ifstream fin("outSenzorTemp.txt");
        string s;
        getline(fin,s);
        if(s != "OK")
        {
            cout<<"Eroare"<<endl;
            return -1;
        } 

        
        // Pauza 1 sec
        sleep(1);
        i++;
    }
    
}