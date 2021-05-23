# SmartAir

## Cerinte sistem
SO: linux  

## Librarii necesare:  
[Nlohmann JSON](https://github.com/nlohmann/json)
```
sudo apt-get install nlohmann-json3-dev
```
G++, Cmake
```
sudo apt-install g++
sudo apt-install cmake
```
[Fistic](http://pistache.io/)
```
sudo add-apt-repository ppa:pistache+team/unstable
sudo apt update
sudo apt install libpistache-dev
```
## Build 
```
g++ smart_air.cpp -o smartair -lpistache -lpthread
```
## Rulare:
``` 
./smartair
```

## Testare
Puteti testa functionarea aplicatiei apeland
```
curl http://localhost:9080/ready
```

## Exemple request
Setare swing la on
```
curl -XPOST http://127.0.0.1:9081/settings/swing/true
```  

Setare temperatura la 25 de grade
```
curl -XPOST 'http://127.0.0.1:9081/settings/temperature/25'
```  

Actualizare matrice temperatura (Apelat de un senzor)
```
curl -XPOST http://127.0.0.1:9081/matrice/[11,22,33,14,21,22,33,14,12,22,31,24,19,21,13,24]
```

Vizualizare setari curente
```
curl -XGET http://127.0.0.1:9081/settings/getAll 
```