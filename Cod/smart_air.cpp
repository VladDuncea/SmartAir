/*
   Viitorul suntem noi
   Smart AC
   using Rares Cristea's example, 12.03.2021
   using Mathieu Stefani's example, 07 février 2016

   Pentru BUILD g++ smart_air.cpp -o smartair -lpistache -lpthread
   Rulare: ./smartair
*/

#include <algorithm>
#include <fstream>
#include <vector>

#include <pistache/net.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/http_headers.h>
#include <pistache/cookie.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>
#include <pistache/common.h>

#include <signal.h>

// Pentru usurinta JSON
#include <nlohmann/json.hpp>

// for convenience
using json = nlohmann::json;

using namespace std;
using namespace Pistache;

// General advice: pay atention to the namespaces that you use in various contexts. Could prevent headaches.

// This is just a helper function to preety-print the Cookies that one of the enpoints shall receive.
void printCookies(const Http::Request& req) {
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto& c: cookies) {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

// Some generic namespace, with a simple function we could use to test the creation of the endpoints.
namespace Generic {

    void handleReady(const Rest::Request&, Http::ResponseWriter response) {
        response.send(Http::Code::Ok, "Aerul merge!\n");
    }

}


///////////////////////////
// Tipuri necesare  // TODO ar trebui mutate in un fisier separat pentru tipuri
// Bool settings
struct boolSetting{
    std::string name;
    bool value;
};

// Int settings
struct intSetting{
    std::string name;
    int value;
};

// Custom settings
enum acMode{
    autoo,
    cool,
    dry,
    heat,
    fan
};
struct modeStruct{
    std::string name;
    acMode value;
};
enum fanSpeedEnum{
    autoFan,
    low,
    medium,
    high
};
struct FanSpeedStruct{
    std::string name;
    fanSpeedEnum value;
};

struct ProgrameMemorie{
    bool economy, working, swing, timer;
    int temperature;
    acMode mode;
    fanSpeedEnum fanSpeed; 
};

struct Rezervor{
    int quantity;
    int numberOfMonths;
};

// Parsare JSON -> programMemorie si invers
void to_json(json& j, const ProgrameMemorie& p) {
    j = json{{"economy", p.economy}, {"working", p.working}, {"swing", p.swing}, 
                {"timer", p.timer},{"temperature", p.temperature},{"mode", p.mode},{"fanSpeed", p.fanSpeed}};
}

void from_json(const json& j, ProgrameMemorie& p) {
    j.at("economy").get_to(p.economy);
    j.at("working").get_to(p.working);
    j.at("swing").get_to(p.swing);
    j.at("timer").get_to(p.timer);
    j.at("temperature").get_to(p.temperature);
    j.at("mode").get_to(p.mode);
    j.at("fanSpeed").get_to(p.fanSpeed);
}

// accesarea campurilor din JSON si salvarea in struct
void from_json_rezervor(const json& j, Rezervor& r) {
    j.at("quantity").get_to(r.quantity);
    j.at("numberOfMonths").get_to(r.numberOfMonths);
}

// END TIPURI
/////////////////

// Definition of the MicrowaveEnpoint class 
class SmartAirEndpoint {
public:
    explicit SmartAirEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    { }

    // Initialization of the server. Additional options can be provided here
    void init(size_t thr = 2) {
        auto opts = Http::Endpoint::options()
            .threads(static_cast<int>(thr));
        httpEndpoint->init(opts);
        // Server routes are loaded up
        setupRoutes();
    }

    // Server is started threaded.  
    void start() {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serveThreaded();

        // Construim un thread care va rula functia de update
        // Foarte important ref(smartAir) -> trimite smartAir ca referinta, nu face copie
        updateLoopThread = new thread(&SmartAir::UpdateLoop, ref(smartAir));
    }

    // When signaled server shuts down
    void stop(){
        httpEndpoint->shutdown();

        // Semnalare oprire thread
        smartAir.runThread = false;
        // Asteptare oprire thread
        updateLoopThread->join();
    }

private:
    thread *updateLoopThread;
    void setupRoutes() {
        using namespace Rest;
        // Defining various endpoints
        // Generally say that when http://localhost:9080/ready is called, the handleReady function should be called. 
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&SmartAirEndpoint::doAuth, this));
        Routes::Post(router, "/settings/:settingName/:value", Routes::bind(&SmartAirEndpoint::setSetting, this));
        Routes::Get(router, "/settings/:settingName/", Routes::bind(&SmartAirEndpoint::getSetting, this));
        Routes::Get(router, "/settings/getAll/", Routes::bind(&SmartAirEndpoint::getRunningConfig, this));

        // Rute pentru programe
        Routes::Post(router, "/programe/alege/:value", Routes::bind(&SmartAirEndpoint::setProgram, this));
        Routes::Get(router, "/programe/memorie", Routes::bind(&SmartAirEndpoint::getProgram, this));
        // TODO Routes::Post(router, "/programe/add", Routes::bind(&SmartAirEndpoint::addProgram, this));

        // Ruta pentru rezervorul de apa
        // forma lui body din Post {"numberOfMonths":3,"quantity":10}
        Routes::Post(router, "/rezervorApa", Routes::bind(&SmartAirEndpoint::setRezervor, this));
    }

    
    void doAuth(const Rest::Request& request, Http::ResponseWriter response) {
        // Function that prints cookies
        printCookies(request);
        // In the response object, it adds a cookie regarding the communications language.
        response.cookies()
            .add(Http::Cookie("lang", "en-US"));
        // Send the response
        response.send(Http::Code::Ok);
    }

    /////////////////////
    // SETARI

    // Endpoint to configure one of the AC's settings.
    void setSetting(const Rest::Request& request, Http::ResponseWriter response){
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        auto settingName = request.param(":settingName").as<std::string>();

        // This is a guard that prevents editing the same value by two concurent threads. 
        Guard guard(smartAirLock);

        
        string val = "";
        if (request.hasParam(":value")) {
            auto value = request.param(":value");
            val = value.as<string>();
        }

        // Setting the microwave's setting to value
        int setResponse = smartAir.set(settingName, val);

        // Sending some confirmation or error response.
        if (setResponse == 1) {
            response.send(Http::Code::Ok, settingName + " was set to " + val);
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found or '" + val + "' was not a valid value ");
        }

    }

    // Setting to get the settings value of one of the configurations of the Microwave
    void getSetting(const Rest::Request& request, Http::ResponseWriter response){
        auto settingName = request.param(":settingName").as<std::string>();

        Guard guard(smartAirLock);

        string valueSetting = smartAir.get(settingName);

        if (valueSetting != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                        .add<Header::Server>("pistache/0.1")
                        .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, settingName + " is " + valueSetting +"\n");
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found" + "\n");
        }
    }

    void getRunningConfig(const Rest::Request& request, Http::ResponseWriter response){
        Guard guard(smartAirLock);

        string stringJSON = smartAir.getRunngingConfig();

        if (stringJSON != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                        .add<Header::Server>("pistache/0.1")
                        .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, stringJSON);
        }
        else {
            response.send(Http::Code::Not_Found, "A aparut o eroare\n");
        }
    }

    // END SETARI
    //////////////////////////

    //////////////////////////
    // PROGRAME

    // Setting to get the settings value of one of the configurations of the Microwave
    void getProgram(const Rest::Request& request, Http::ResponseWriter response){
        Guard guard(smartAirLock);

        string stringJSON = smartAir.programeToJSON();

        if (stringJSON != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                        .add<Header::Server>("pistache/0.1")
                        .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, stringJSON);
        }
        else {
            response.send(Http::Code::Not_Found, "A aparut o eroare\n");
        }
    }

    // Endpoint to load the settings from a given preset
    void setProgram(const Rest::Request& request, Http::ResponseWriter response){
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        int nrProgram = request.param(":value").as<std::int16_t>();

        // This is a guard that prevents editing the same value by two concurent threads. 
        Guard guard(smartAirLock);

        // Setting the microwave's setting to value
        int setResponse = smartAir.aplicaProgram(nrProgram);

        // Sending some confirmation or error response.
        if (setResponse == 1) {
            response.send(Http::Code::Ok, "Settings were loaded from the preset nr " + to_string(nrProgram));
        }
        else {
            response.send(Http::Code::Not_Found,"The program with number " + to_string(nrProgram) + " was not found");
        }
    }

    // END PROGRAME
    ///////////////////////

    // Endpoint to load the settings from a given preset
    void setRezervor(const Rest::Request& request, Http::ResponseWriter response){
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        string requestRezervor = request.body();
        Rezervor rezervor;

        json y = json::parse(requestRezervor);

        // This is a guard that prevents editing the same value by two concurent threads. 
        Guard guard(smartAirLock);

        from_json_rezervor(y, rezervor);

        // // Setting the microwave's setting to value
        int setResponse = -1;
        if(rezervor.quantity > 0 && rezervor.numberOfMonths > 0){
            setResponse = 1;
        }
        
        // Sending some confirmation or error response.
        if (setResponse == 1) {
            response.send(Http::Code::Ok, "S-a facut umplerea rezervorului cu " + to_string(rezervor.quantity) 
                        + " litri. Urmatoarea schimbare se va face peste " + to_string(rezervor.numberOfMonths) + " luni.");
        }
        else {
            response.send(Http::Code::Not_Found,"Rezervorul nu poate primi valori negative.");
        }
    }


    // END REZERVOR APA
    ///////////////////////

    // Defining the class of the AC. It should model the entire configuration of the AC
    class SmartAir {
    private:
        // Bool settings
        boolSetting economy,working,swing,timer;
        // Int settings
        intSetting temperature;
        modeStruct mode;
        FanSpeedStruct fanSpeed;

        vector<ProgrameMemorie> programeMemorie;
        // Cale fisier programe
        const string locatiePrograme = "programe.txt";

    public:

        // Pentru thread
        bool runThread = true;

        explicit SmartAir()
        {
            // Initializare variabile (numele lor)
            // TODO: implementeaza memoria pentru a pastra ultimele setari
            economy.name    = "economy";
            working.name    = "onoff";
            swing.name      = "swing";
            timer.name      = "timer";

            temperature.name = "temperature";

            mode.name = "mode";
            fanSpeed.name = "fanSpeed";

            // Citire programe
            citestePrograme();

            // Seteaza variabilele la programul default
            // TODO (de pus ultimul program)
            aplicaProgram(0);
        }

        ///////////////////////////
        // Functii pentru programe

        void citestePrograme()
        {
            ifstream fin(locatiePrograme);
            // Citim numarul de programe existente
            int nrPrograme;
            fin >>  nrPrograme;
            for(int i=0;i < nrPrograme;i++)
            {
                // Citim pe rand datele necesare
                ProgrameMemorie p;
                int mode, fan;
                fin >> p.economy >> p.working >> p.swing >> p.timer;
                fin >> p.temperature >> mode >> fan;
                p.mode = (acMode) mode;
                p.fanSpeed = (fanSpeedEnum) fan;

                programeMemorie.push_back(p);
            }
        }

        int aplicaProgram(int nrProgram)
        {
            // Intoarce: -1 daca programul nu exista, 1 daca totul e ok
            if(nrProgram >= programeMemorie.size())
            {
                return -1;
            }

            ProgrameMemorie &p = programeMemorie[nrProgram];
            economy.value       = p.economy;
            working.value       = p.working;
            swing.value         = p.swing;
            timer.value         = p.timer;
            temperature.value   = p.temperature;
            mode.value          = p.mode;
            fanSpeed.value      = p.fanSpeed;

            return 1;
        }

        string programeToJSON()
        {
            // Pentru ca am scris functia to_json se va face conversia automat!, 
            // Functia to_json trebuie sa fie vizibila(publica)
            json j(programeMemorie);

            return j.dump();
        }
        
        // END Functii pentru programe
        ///////////////////////////

        // Setting the value for one of the settings. Hardcoded for the defrosting option
        int set(std::string name, std::string value){
            // Economy
            if(economy.name == name){
                if(value == "true")
                {
                    economy.value = true;
                    return 1;
                }
                if(value == "false"){
                    economy.value = false;
                    return 1;
                }
            }
            // On/Off
            if(working.name == name){
                if(value == "true")
                {
                    working.value = true;
                    return 1;
                }
                if(value == "false"){
                    working.value = false;
                    return 1;
                }
            }
            // Swing
            if(swing.name == name){
                if(value == "true")
                {
                    swing.value = true;
                    return 1;
                }
                if(value == "false"){
                    swing.value = false;
                    return 1;
                }
            }
            // Timer
            if(timer.name == name){
                if(value == "true")
                {
                    timer.value = true;
                    return 1;
                }
                if(value == "false"){
                    timer.value = false;
                    return 1;
                }
            }

            // Temperature
            if(temperature.name == name){
                try{
                    int intvalue = std::stoi(value);
                    // Temp range is between 15 and 30
                    if(intvalue > 15 && intvalue < 30 )
                    {
                        temperature.value = intvalue;
                        return 1;
                    }
                }
                catch(std::exception)
                {
                    return 0;
                }
            }

            // Mode
            if(mode.name == name)
            {
                if(value == "auto")
                {
                    mode.value = acMode::autoo;
                    return 1;
                }
                if(value == "cool")
                {
                    mode.value = acMode::cool;
                    return 1;
                }
                if(value == "dry")
                {
                    mode.value = acMode::dry;
                    return 1;
                }
                if(value == "heat")
                {
                    mode.value = acMode::heat;
                    return 1;
                }
                if(value == "fan")
                {
                    mode.value = acMode::fan;
                    return 1;
                }
            }

            // Fan speed
            if(fanSpeed.name == name)
            {
                if(value == "auto")
                {
                    fanSpeed.value = fanSpeedEnum::autoFan;
                    return 1;
                }
                if(value == "low")
                {
                    fanSpeed.value = fanSpeedEnum::low;
                    return 1;
                }
                if(value == "medium")
                {
                    fanSpeed.value = fanSpeedEnum::medium;
                    return 1;
                }
                if(value == "high")
                {
                    fanSpeed.value = fanSpeedEnum::high;
                    return 1;
                }
            }
            return 0;
        }

        // Getter
        string get(std::string name){
            // Economy
            if (name == economy.name){
                return std::to_string(economy.value);
            }
            // On/Off
            if (name == working.name){
                return std::to_string(working.value);
            }
            // Swing
            if (name == swing.name){
                return std::to_string(swing.value);
            }
            // Timer
            if (name == timer.name){
                return std::to_string(timer.value);
            }

            // Temperature
            if(name == temperature.name)
            {
                return std::to_string(temperature.value);
            }

            // Mode
            if( name == mode.name)
            {
                switch(mode.value)
                {
                    case acMode::autoo:
                        return "AUTO";
                    case acMode::cool:
                        return "COOL";
                    case acMode::dry:
                        return "DRY";
                    case acMode::heat:
                        return "HEAT";
                    case acMode::fan:
                        return "FAN";
                }
            }

            // FAN SPEED
            if( name == fanSpeed.name)
            {
                switch(fanSpeed.value)
                {
                    case fanSpeedEnum::autoFan:
                        return "AUTO";
                    case fanSpeedEnum::low:
                        return "LOW";
                    case fanSpeedEnum::medium:
                        return "MEDIUM";
                    case fanSpeedEnum::high:
                        return "HIGH";
                }
            }

            return "";
        }
    
        string getRunngingConfig()
        {
            json j;
            j["economy"]        = economy.value;
            j["working"]        = working.value;
            j["swing"]          = swing.value;
            j["timer"]          = timer.value;
            j["temperature"]    = temperature.value;
            j["mode"]           = mode.value;
            j["fanSpeed"]       = fanSpeed.value;
            j["timer"]          = timer.value;

            return j.dump();
        }

        void UpdateLoop()
        {
            while(runThread)
            {
                // Rulam functia de update la aproximativ o secunda
                this_thread::sleep_for (chrono::seconds(1));

                // TODO aici vor veni toate interactiunile periodice
            }

            cout << "Am oprit threadul de update!";
        }
    };

    // Create the lock which prevents concurrent editing of the same variable
    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock smartAirLock;

    // Instance of the microwave model
    SmartAir smartAir;

    // Defining the httpEndpoint and a router.
    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

int main(int argc, char *argv[]) {

    // This code is needed for gracefull shutdown of the server when no longer needed.
    sigset_t signals;
    if (sigemptyset(&signals) != 0
            || sigaddset(&signals, SIGTERM) != 0
            || sigaddset(&signals, SIGINT) != 0
            || sigaddset(&signals, SIGHUP) != 0
            || pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        perror("install signal handler failed");
        return 1;
    }

    // Set a port on which your server to communicate
    Port port(9080);

    // Number of threads used by the server
    int thr = 2;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc == 3)
            thr = std::stoi(argv[2]);
    }

    Address addr(Ipv4::any(), port);

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;
    cout << "Listening on port: " << port << " and ip: " << addr.host() << endl;

    // Instance of the class that defines what the server can do.
    SmartAirEndpoint stats(addr);

    // Initialize and start the server
    stats.init(thr);
    stats.start();


    // Code that waits for the shutdown sinal for the server
    int signal = 0;
    int status = sigwait(&signals, &signal);
    if (status == 0)
    {
        std::cout << "received signal " << signal << std::endl;
    }
    else
    {
        std::cerr << "sigwait returns " << status << std::endl;
    }

    stats.stop();
}
