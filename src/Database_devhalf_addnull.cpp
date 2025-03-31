#include "Database.h"
using namespace db;

#define MAX_TIMES_OF_SELECT_PLB_FOR_BIPARTITE 20
#define MIN_SIEZE_OF_LUT_FOR_BIPARTITE 4
#define COST_BASE 1000
#define MAX_FIXED 4
#include "Database.h"
using namespace db;
Database db::database;
Database::Database() {
    sitemap_nx = 0;
    sitemap_ny = 0;
    crmap_nx = 0;
    crmap_ny = 0;
}
Database::~Database() {
    for (auto m : masters) delete m;
    for (auto i : instances) delete i;
    for (auto n : nets) delete n;
    for (auto s : sitetypes) delete s;
    for (auto r : resources) delete r;
    for (auto p : packs) delete p;
    for (unsigned int i = 0; i < sites.size(); ++i)
        for (unsigned int j = 0; j < sites[i].size(); ++j)
            if (j == 0 || sites[i][j] != sites[i][j - 1]) delete sites[i][j];
}
void Database::setup() {
    for (int x = 0; x < sitemap_nx; x++) {
        for (int y = 1; y < sitemap_ny; y++) {
            if (sites[x][y] == NULL && sites[x][y - 1] != NULL) {
                sites[x][y] = sites[x][y - 1];
                // sites[x][y]->h++;
            }
        }
    }
    std::cout << "sitemap over,";
    tdmap_nx = sitemap_nx;
    tdmap_ny = sitemap_ny;
    targetDensity.resize(tdmap_nx, vector<double>(tdmap_ny, 1.0));

    for (int x = 0; x < crmap_nx; x++) {
        for (int y = 0; y < crmap_ny; y++) {
            clkrgns[x][y]->Init();
        }
    }
    clkidx_row = new int[6]{0,26,54,86,114,150};
    clkidx_col = new int[6]{0,60,120,180,240,300};
    std::cout << "clkrgns Init over,";

    /*
    //remove clock net
    bool clkFound = false;
    for(int i=0; i<(int)nets.size(); i++){
        int nPins = (int)nets[i]->pins.size();
        for(int p=0; p<nPins; p++){
            if(nets[i]->pins[p]->type->type == 'c'){
                clkFound = true;
                break;
            }
        }
        if(clkFound){
            printlog(LOG_INFO, "clock net found: %s with %d pins", nets[i]->name.c_str(), (int)nets[i]->pins.size());
            delete nets[i];
            nets.erase(nets.begin()+i);
            break;
            //i--;
            //clkFound = false;
        }
    }
    */

    for (unsigned i = 0; i < instances.size(); ++i) instances[i]->id = i;
    for (unsigned i = 0; i < sitetypes.size(); ++i) sitetypes[i]->id = i;
    for (unsigned i = 0; i < nets.size(); ++i) nets[i]->id = i;
    std::cout << "Id init over" << std::endl;

    // setPinMap();
    // setSupplyAll();
    setCellPinIndex();
}
/***** Resource *****/
Resource::Name Resource::NameString2Enum(const string &name) {
// LUT6, DFF, CARRY4, DRAM, DSP, RAMA, RAMB, GCLK, IOA, IOB, IPPIN
    if (name == "LUT6")
        return LUT6;
    else if (name == "DFF")
        return DFF;
    else if (name == "CARRY4")
        return CARRY4;
    else if (name == "DRAM")
        return DRAM;
    else if (name == "DSP")
        return DSP;
    else if (name == "RAMA")
        return RAMA;
    else if (name == "RAMB")
        return RAMB;
    else if (name == "GCLK")
        return GCLK;
    else if (name == "IOA")
        return IOA;
    else if (name == "IOB")
        return IOB;
    else if (name == "IPPIN")
        return IPPIN;
    else {
        cerr << "unknown master name: " << name << endl;
        exit(1);
    }
}

string resourceNameStrs[] = {"LUT6", "DFF", "CARRY4", "DRAM", "DSP", "RAMA", "RAMB", "GCLK",
                             "IOA", "IOB", "IPPIN", "PLB"};
string Resource::NameEnum2String(const Resource::Name &name) { return resourceNameStrs[name]; }

Resource::Resource(Name n) { this->name = n; }

Resource::Resource(const Resource &resource) {
    name = resource.name;
    masters = resource.masters;
}

void Resource::addMaster(Master *master) {
    masters.push_back(master);
    master->resource = this;
}

/***** SiteType *****/
SiteType::Name SiteType::NameString2Enum(const string &name) {
    if (name == "PLB")
        return PLB;
    else if (name == "DSP")
        return DSP;
    else if (name == "RAMA")
        return RAMA;
    else if (name == "RAMB")
        return RAMB;
    else if (name == "IO")
        return IO;
    else if (name == "IOA")
        return IOA;
    else if (name == "IOB")
        return IOB;
    else if (name == "IPPIN")
        return IPPIN;
    else if (name == "GCLK")
        return GCLK;
    else {
        cerr << "unknown site type name: " << name << endl;
        exit(1);
    }
}

string siteTypeNameStrs[] = {"DSP", "RAMB", "RAMA", "IO", "IOA", "IOB", "GCLK", "PLB", "IPPIN"};
string SiteType::NameEnum2String(const SiteType::Name &name) { return siteTypeNameStrs[name]; }

SiteType::SiteType(Name n) {
    this->name = n;
}

SiteType::SiteType(const SiteType &sitetype) {
    name = sitetype.name;
    weight = sitetype.weight;
    height = sitetype.height;
    resources = sitetype.resources;
}

void SiteType::addResource(Resource *resource) {
    resources.push_back(resource);
    resource->siteType = this;
}
void SiteType::addResource(Resource *resource, int count) {
    //���resource������site������
    if (resource->name == Resource::LUT6) {
        count*=2;
    }
    for (int i = 0; i < count; i++) {
        addResource(resource);
    }
}
void SiteType::addMaster(Master *master) {
    masters.push_back(master);
    master->siteType = this;
}
void SiteType::addMaster(Master *master, int count) {
    //���resource������site������
    for (int i = 0; i < count; i++) {
        addMaster(master);
    }
}

bool SiteType::matchInstance(Instance *instance) {
    for (auto r : resources) {
        if (instance->master->resource == r) {
            return true;
        }
    }
    return false;
}

/***** Site *****/
Site::Site() {
    type = NULL;
    pack = NULL;
    x = -1;
    y = -1;
    w = 0;
    h = 0;
}

Site::Site(int x, int y, SiteType *sitetype) {
    this->x = x;
    this->y = y;
    this->w = sitetype->weight;
    this->h = sitetype->height;
    this->type = sitetype;
    this->pack = NULL;
    this->formuti = new Site();
    this->formuti->x = x;
    this->formuti->y = y;
}

/***** Pack *****/

Pack::Pack() {
    type = NULL;
    site = NULL;
}

Pack::Pack(SiteType *sitetype, Site *site) {
    this->instances.resize(sitetype->resources.size(), NULL);
    this->type = sitetype;
    this->site = site;
}

void Pack::print() {
    string instList = "";
    int nInst = 0;
    for (auto inst : instances)
        if (inst != NULL) {
            nInst++;
            instList = instList + " " + to_string(inst->id);
        }
    // printlog(LOG_INFO, "(x,y)=(%lf,%lf), %d instances=[%s]", site->cx(), site->cy(), nInst, instList.c_str());
}

bool Pack::IsEmpty() {
    bool isEmptyPack = true;
    for (auto inst : instances) {
        if (inst != NULL) {
            isEmptyPack = false;
            break;
        }
    }
    return isEmptyPack;
}
void Pack::addInstance(Instance * instance) {
    if (this->type->name == SiteType::PLB) {
        // cout << this->site->x << ',' << this->site->y << ":" << this->instances.size() << instance->name ;
        if (instance->master->name == Master::LUT6||instance->master->name == Master::LUT6X) {
            for (int i = 0; i <= 1; i++) {
                site->pack->instances[2*(instance->z)+i] = instance;
            }
            instance->packz = 2*(instance->z);
            if (instance->z <=3) instance->bankz = 0;
            else instance->bankz = 1;
        } else if (instance->master->name == Master::CARRY4) {//CARRY��32��33�±�
            site->pack->instances[32+(instance->z)] = instance;
            instance->packz = 32+(instance->z);
            instance->bankz = 3;
        } else if (instance->master->name == Master::DRAM) {
            //LUT*4+CARRY(Z0��ʾbank0���ĸ�LUT0��3�����λ��Z1�Ͷ�ӦLUT4��7)
            for (int i = 0 ; i < 8; i++) {
                site->pack->instances[8*(instance->z)+i] = instance;
            }
            instance->packz = 8*(instance->z);
            if (instance->z == 0) instance->bankz = 0;
            else instance->bankz = 1;
        } else if (instance->master->name == Master::SEQ){ //DFF
            if (site->pack->instances[16+(instance->z)] == NULL) {
                site->pack->instances[16+(instance->z)] = instance;
                instance->packz = 16+(instance->z);
            } else {
                site->pack->instances[16+(instance->z)+1] = instance;
                instance->packz = 16+(instance->z)+1;
            }
            if (instance->packz<= 23) {
                instance->bankz = 0;
            } else if (24<=instance->packz) {
                instance->bankz = 1;
            }
        } else if (instance->master->name == Master::F7MUX) { //F7MUX
            for (int i = 0 ; i < 4; i++) {
                site->pack->instances[4*(instance->z)+i] = instance;
            }
            if (instance->z <= 1) instance->bankz = 0;
            else instance->bankz = 1;
        } else if (instance->master->name == Master::F8MUX) { //F8MUX
            for (int i = 0 ; i < 8; i++) {
                site->pack->instances[8*(instance->z)+i] = instance;
            }
            instance->packz = 8*(instance->z);
            if (instance->z == 0) instance->bankz = 0;
            else instance->bankz = 1;
        }else { //lut1-5
            if (site->pack->instances[2*(instance->z)] == NULL) {
                site->pack->instances[2*(instance->z)] = instance;
                instance->packz = 2*(instance->z);
            } else {
                site->pack->instances[2*(instance->z)+1] = instance;
                instance->packz = 2*(instance->z)+1;
            }
            // cout << instance->z <<endl;
            if (instance->z <=3) {
                instance->bankz = 0;
            } else if (4<=instance->z) {
                instance->bankz = 1;
            }
            // if (instance->name == "inst_185") {
            //     cout << instance->z << endl;
            //     cout << instance->bankz << endl;
            // }
        }
        // cout << "map over" << endl;
    } else { // ����sitetype
        this->instances[instance->z] = instance;
        instance->packz = instance->z;
    }
}
/***** Master *****/
Master::Name Master::NameString2Enum(const string &name) {
    if (name == "LUT1")
        return LUT1;
    else if (name == "LUT2")
        return LUT2;
    else if (name == "LUT3")
        return LUT3;
    else if (name == "LUT4")
        return LUT4;
    else if (name == "LUT5")
        return LUT5;
    else if (name == "LUT6")
        return LUT6;
    else if (name == "LUT6X")
        return LUT6X;
    else if (name == "DRAM")
        return DRAM;
    else if (name == "CARRY4")
        return CARRY4;
    else if (name == "DSP")
        return DSP;
    else if (name == "F7MUX")
        return F7MUX;
    else if (name == "F8MUX")
        return F8MUX;
    else if (name == "GCLK")
        return GCLK;
    else if (name == "IOA")
        return IOA;
    else if (name == "IOB")
        return IOB;
    else if (name == "IPPIN")
        return IPPIN;
    else if (name == "PLB")
        return PLB;
    else if (name == "RAMA")
        return RAMA;
    else if (name == "RAMB")
        return RAMB;
    else if (name == "SEQ"||name == "DFF")
        return SEQ;
    else {
        cerr << "unknown master name: " << name << endl;
        exit(1);
    }
}

string masterNameStrs[] = {
        "LUT1", "LUT2", "LUT3", "LUT4", "LUT5", "LUT6", "LUT6X",
        "CARRY4",    // CARRY
        "DRAM",  // RAM
        "DSP",
        "F7MUX",
        "F8MUX",
        "GCLK",
        "IOA",
        "IOB",
        "IPPIN",
        "PLB",
        "RAMA",
        "RAMB",
        "SEQ"};
string Master::NameEnum2String(const Master::Name &name) { return masterNameStrs[name]; }

Master::Master(Name n) {
    this->name = n;
    this->resource = NULL;
}

Master::Master(const Master &master) {
    name = master.name;
    resource = master.resource;
    pins.resize(master.pins.size());
    for (int i = 0; i < (int)master.pins.size(); i++) {
        pins[i] = new PinType(*master.pins[i]);
    }
}

Master::~Master() {
    for (int i = 0; i < (int)pins.size(); i++) {
        delete pins[i];
    }
}

void Master::addPin(PinType &pin) { pins.push_back(new PinType(pin)); }
bool Master::addResource(Resource *resource) {
    // cout << Master::NameEnum2String(this->name) << endl;
    // cout << Resource::NameEnum2String(resource->name) << endl;
    switch (this->name)
    {
        case LUT1:
        case LUT2:
        case LUT3:
        case LUT4:
        case LUT5:
        case LUT6:
        case LUT6X:
            if (resource->name == Resource::LUT6) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case CARRY4:
            if (resource->name == Resource::CARRY4) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case SEQ:
            if (resource->name == Resource::DFF) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case DRAM:
            if (resource->name == Resource::LUT6) {
                resource->addMaster(this);
                this->occupyResourceNum = 4;
                return true;
            }
            return false;
        case F7MUX:
            if (resource->name == Resource::LUT6) {
                resource->addMaster(this);
                this->occupyResourceNum = 2;
                return true;
            }
            return false;
        case F8MUX:
            if (resource->name == Resource::LUT6) {
                resource->addMaster(this);
                this->occupyResourceNum = 4;
                return true;
            }
            return false;
        case DSP:
            if (resource->name == Resource::DSP) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case GCLK:
            if (resource->name == Resource::GCLK) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case IOA:
            if (resource->name == Resource::IOA) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case IOB:
            if (resource->name == Resource::IOB) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case IPPIN:
            if (resource->name == Resource::IPPIN) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case RAMA:
            if (resource->name == Resource::RAMA) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case RAMB:
            if (resource->name == Resource::RAMB) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        case PLB:
            if (resource->name == Resource::PLB) {
                resource->addMaster(this);
                this->occupyResourceNum = 1;
                return true;
            }
            return false;
        default:
            return false;
    }
}
PinType *Master::getPin(const string &name) {
    for (int i = 0; i < (int)pins.size(); i++) {
        if (pins[i]->name == name) {
            return pins[i];
        }
    }
    return NULL;
}

/***** Instance *****/
Instance::Instance() {
    id = -1;
    master = NULL;
    pack = NULL;
    slot = -1;
    fixed = false;
    inputFixed = false;
    x = 0;
    y = 0;
    z = 0;
    packz = 0;
    bankz = -1;
}

Instance::Instance(const string &name, Master *master) {
    this->id = -1;
    this->name = name;
    this->master = master;
    this->pack = NULL;
    this->slot = -1;
    this->fixed = false;
    this->inputFixed = false;
    this->x = 0;
    this->y = 0;
    this->z = 0;
    this->packz = 0;
    this->bankz = -1;
    this->pins.resize(master->pins.size());
    this->IpinLens.resize(master->pins.size(),0.0);
    this->OpinLens.resize(master->pins.size(),0.0);
    for (int i = 0; i < (int)master->pins.size(); i++) {
        this->pins[i] = new Pin(this, i);
    }
}

Instance::Instance(const Instance &instance) {
    id = -1;
    name = instance.name;
    master = instance.master;
    pack = NULL;
    slot = -1;
    fixed = instance.fixed;
    x = 0;
    y = 0;
    z = 0;
    packz = 0;
    bankz = -1;
    inputFixed = instance.inputFixed;
    pins.resize(instance.pins.size());
    IpinLens.resize(instance.pins.size());
    for (int i = 0; i < (int)instance.pins.size(); i++) {
        // pins[i] = new Pin(*instance.pins[i]);
        pins[i] = new Pin(this, i);
    }
}

Instance::~Instance() {
    for (int i = 0; i < (int)pins.size(); i++) {
        delete pins[i];
    }
}

Pin *Instance::getPin(const string &name) {
    for (int i = 0; i < (int)pins.size(); i++) {
        if (pins[i]->type->name == name) {
            return pins[i];
        }
    }
    return NULL;
}

bool Instance::matchSiteType(SiteType *type) { return type->matchInstance(this); }

void Instance::getIPinLen() {
    for (int i = 0; i < this->pins.size()-1; i++) {
        Pin *p = this->pins[i];
        if (p->net == NULL) continue;
        Instance *oinstance = p->net->pins[0]->instance;
        IpinLens[i] = abs(oinstance->x - this->x) + abs(oinstance->y - this->y);
        auto mi = oinstance->name_instances.find(this->name);
        oinstance->OpinLens[mi->second] = IpinLens[i];
    }
}
void Instance::getOPinlen() {
    if (this->outPin == NULL) return;
    Net *net = this->outPin->net;
    for (int i = 1; i < net->pins.size(); i++) {
        Instance *instance = net->pins[i]->instance;
        OpinLens[i] = abs(instance->x - this->x) + abs(instance->y - this->y);
        string name = net->pins[i]->type->name;
        int pindex =  std::stoi(name.substr(name.find("_")+1));
        // cout << instance->name << ',' << name << ',' << instance->x << ',' << instance->y;
        // cout << "prelen:" << instance->IpinLens[pindex] << ',';
        instance->IpinLens[pindex] = OpinLens[i];
        // cout << "aftlen:" << instance->IpinLens[pindex] << endl;
    }
}
double Instance::getTotalH() {
    double totalHPWL = 0;
    for (int i = 0; i < IpinLens.size(); i++) {
        // cout << this->name << " IpinLens[" << i << "]: " << IpinLens[i] << endl;
        totalHPWL += IpinLens[i];
    }
    for (int j = 0; j < OpinLens.size(); j++) {
        // cout << this->name << " OpinLens[" << j << "]: " << OpinLens[j] << endl;
        totalHPWL += OpinLens[j];
    }
    // cout << this->name << " totalHPWL: " << totalHPWL << endl;
    return totalHPWL;
}
/***** PinType *****/
PinType::PinType() {
    type = 'x';
    arr = "";
    isTiming = false;
}

PinType::PinType(const string &name, string type, string arr) {
    this->name = name;
    this->type = type;
    this->arr = arr;
    this->isTiming = false;
}

PinType::PinType(const PinType &pintype) {
    name = pintype.name;
    type = pintype.type;
    arr = pintype.arr;
    isTiming = pintype.isTiming;
}

/***** Pin *****/
Pin::Pin() {
    this->instance = NULL;
    this->net = NULL;
    this->type = NULL;
    this->isTiming = false;
}

Pin::Pin(Instance *instance, int i) {
    this->instance = instance;
    this->net = NULL;
    this->type = instance->master->pins[i];
    this->isTiming = false;
}

Pin::Pin(Instance *instance, const string &pin) {
    this->instance = instance;
    this->net = NULL;
    this->type = instance->master->getPin(pin);
    this->isTiming = false;
}

Pin::Pin(const Pin &pin) {
    instance = pin.instance;
    net = pin.net;
    type = pin.type;
    isTiming = pin.isTiming;
}

/***** Net *****/
Net::Net() {}

Net::Net(const string &name) {
    this->name = name;
    this->pins.resize(1, NULL);
    this->isClk = false;
}

Net::Net(const Net &net) {
    name = net.name;
    pins = net.pins;
    isClk = net.isClk;
    id = net.id;
}

Net::~Net() {}


/** Function of class **/
void Net::addPin(Pin *pin) {
    if (pin->type->type == "OUTPUT") {
        if (pins[0] != NULL) {
            cerr << "source pin duplicated" << endl;
        } else {
            pins[0] = pin;
            pin->net = this;
            return;
        }
    }
    pins.push_back(pin);
    pin->net = this;
}

Master *Database::getMaster(Master::Name name) {
    auto mi = name_masters.find(name);
    if (mi == name_masters.end()) return NULL;
    return mi->second;
}
Instance *Database::getInstance(const string &name) {
    auto mi = name_instances.find(name);
    if (mi == name_instances.end()) {
        return NULL;
    }
    return mi->second;
}
Net *Database::getNet(const string &name) {
    auto mi = name_nets.find(name);
    if (mi == name_nets.end()) {
        return NULL;
    }
    return mi->second;
}
Resource *Database::getResource(Resource::Name name) {
    auto mi = name_resources.find(name);
    if (mi == name_resources.end()) return NULL;
    return mi->second;
}
SiteType *Database::getSiteType(SiteType::Name name) {
    auto mi = name_sitetypes.find(name);
    if (mi == name_sitetypes.end()) return NULL;
    return mi->second;
}
Master *Database::addMaster(const Master &master) {
    if (getMaster(master.name) != NULL) {
        cerr << "Master::Name: " << master.name << " duplicated" << endl;
        return NULL;
    }
    Master *newmaster = new Master(master);
    name_masters[master.name] = newmaster;
    masters.push_back(newmaster);
    return newmaster;
}
Instance *Database::addInstance(const Instance &instance) {
    if (getInstance(instance.name) != NULL) {
        cerr << "Instance: " << instance.name << " duplicated" << endl;
        return NULL;
    }
    Instance *newinstance = new Instance(instance);
    name_instances[instance.name] = newinstance;
    instances.push_back(newinstance);
    return newinstance;
}
Net *Database::addNet(const Net &net) {
    if (getNet(net.name) != NULL) {
        cerr << "Net: " << net.name << " duplicated" << endl;
        return NULL;
    }
    Net *newnet = new Net(net);
    name_nets[net.name] = newnet;
    nets.push_back(newnet);
    return newnet;
}
Resource *Database::addResource(const Resource &resource) {
    if (getResource(resource.name) != NULL) {
        cerr << "Resource: " << resource.name << " duplicated" << endl;
        return NULL;
    }
    Resource *newresource = new Resource(resource);
    name_resources[resource.name] = newresource;
    resources.push_back(newresource);
    return newresource;
}
SiteType *Database::addSiteType(const SiteType &sitetype) {
    if (getSiteType(sitetype.name) != NULL) {
        cerr << "SiteType: " << sitetype.name << " duplicated" << endl;
        return NULL;
    }
    SiteType *newsitetype = new SiteType(sitetype);
    name_sitetypes[sitetype.name] = newsitetype;
    sitetypes.push_back(newsitetype);
    return newsitetype;
}
Site *Database::addSite(int x, int y, SiteType *sitetype) {
    if (x < 0 || x >= sitemap_nx || y < 0 || y >= sitemap_ny) {
        // printlog(LOG_ERROR, "Invalid position (%d,%d) (w=%d h=%d)", x, y, sitemap_nx, sitemap_ny);
        return NULL;
    }
    if (sites[x][y] != NULL) {
        // site can be repeatly defined, simply ignore it �������
        // printlog(LOG_ERROR, "Site already defined at %d,%d", x, y);
        sites[x][y]->formuti->type = sitetype;
        return sites[x][y]->formuti;
    }
    Site *newsite = new Site(x, y, sitetype);
    sites[x][y] = newsite;
    return newsite;
}
Site *Database::getSite(int x, int y) {
    if (x < 0 || x >= sitemap_nx || y < 0 || y >= sitemap_ny) {
        return NULL;
    }
    return sites[x][y];
}
void Database::setSiteMap(int nx, int ny) {
    sitemap_nx = nx;
    sitemap_ny = ny;
    sites.resize(nx, vector<Site *>(ny, NULL));
}
Instance *Database::getInstance(int x, int y, int z) {
    if (x < 0 || x >= sitemap_nx || y < 0 || y >= sitemap_ny) {
        return NULL;
    }
    return sites[x][y]->pack->instances[z];
}
/**** ClkRgn ******/
ClkRgn::ClkRgn(string _name, int _lx, int _ly, int _hx, int _hy, int _cr_x, int _cr_y) {
    name = _name;
    lx = _lx;
    ly = _ly;
    hx = _hx;
    hy = _hy;
    cr_x = _cr_x;
    cr_y = _cr_y;
    // std::cout << "ophf" << ',' << lx << ',' << ly << ',' << hx << ',' << hy;
    // hfcols.assign(hx - lx, vector<HfCol*>(2, NULL));
    // for (int x = lx; x < hx; x++) {
    //     for (int y = 0; y < 2; y++) {
    //         hfcols[x][ly / 30 + y] = new HfCol(x, ly + y * 30, ly + (y + 1) * 30, this);
    //         hfcols[x - lx][y] = hfcols[x][ly / 30 + y];
    //     }
    // }
    /* ����֪��hfcols��ʲô��˼ ������ע����*/
    // std::cout << "hffinish" << std::endl;
}

void ClkRgn::Init() {
    for (int x = lx; x < hx; x++) {
        for (int y = ly; y < hy; y++) {
            sites.push_back(database.getSite(x, y));
        }
    }
    // for (int x = lx; x < hx; x++) {
    //     for (int y = 0; y < 2; y++) {
    //         hfcols[x - lx][y]->Init();
    //     }
    // }
}

HfCol::HfCol(int _x, int _ly, int _hy, ClkRgn* _clkrgn) {
    x = _x;
    ly = _ly;
    hy = _hy;
    clkrgn = _clkrgn;
}

void HfCol::Init() {
    for (int y = ly; y < hy; y++) {
        sites.push_back(database.getSite(x, y));
    }
}
bool Group::IsBLE() { return instances[0]->IsLUTFF(); }
SiteType::Name Group::GetSiteType() const {
    for (auto inst : instances)
        if (inst != NULL) return inst->master->resource->siteType->name;

    return SiteType::DSP;
}

/**************utils***************/
void Database::setCellPinIndex() {
    Master *FF = getMaster(Master::SEQ);
    if (FF == NULL) {
        // printlog(LOG_ERROR, "FF not found");
    } else {
        for (unsigned int i = 0; i < FF->pins.size(); ++i) {
            if (FF->pins[i]->arr == "CLOCK") {
                clkPinIdx = i;
            } else if (FF->pins[i]->arr == "RESET") {
                srPinIdx = i;
            } else if (FF->pins[i]->arr == "CTRL") {
                cePinIdx = i;
            } else if (FF->pins[i]->arr == "" && FF->pins[i]->type == "INPUT") {
                ffIPinIdx = i;
            }
        }
    }

}
bool Group::IsTypeMatch(Site* site) const { return site->type->name == GetSiteType(); }
// for LUT (#input <= 6), brute����׾�� force (O(m*n)) is efficient
int Database::NumDupInputs(const Instance &lhs, const Instance &rhs) {
    int numDup = 0;
    vector<Net *> nets1;
    for (auto pin1 : lhs.pins)
        if (pin1->type->type == "INPUT") nets1.push_back(pin1->net);
    //�Ҽ�����ͬ������
    for (auto pin2 : rhs.pins)
        if (pin2->type->type == "INPUT") {
            auto pos1 = find(nets1.begin(), nets1.end(), pin2->net);
            if (pos1 != nets1.end()) {
                ++numDup;
                *pos1 = NULL;
            }
        }
    return numDup;
}
bool Database::IsLUTCompatible(const Instance &lhs, const Instance &rhs) {
    int tot = lhs.pins.size() + rhs.pins.size();
    return (tot <= 7) || (tot - 2 - NumDupInputs(lhs, rhs) <= 5);
}

bool Database::isPackValid(Pack *pack) {
    if (pack->type->name ==  SiteType::PLB) {
        /***** LUT Rule *****/
        for (int i = 0; i < 8; i++) {
            Instance *a = pack->instances[2 * i];
            Instance *b = pack->instances[2 * i + 1];
            if (a == NULL || b == NULL) continue;
            if (a->master->name == Master::LUT6) return false;
            if (!IsLUTCompatible(*a, *b)) return false;
        }
        /***** FF Rule *****/
        int ceset[4][4] = {{16, 18, 20, 22}, {17, 19, 21, 23}, {24, 26, 28, 30}, {25, 27, 29, 31}};
        int ckset[2][8] = {{16, 17, 18, 19, 20, 21, 22, 23}, {24, 25, 26, 27, 28, 29, 30, 31}};  // srset is the same
        for (int i = 0; i < 4; i++) {
            Net *CENet = NULL;
            for (int j = 0; j < 4; j++) {
                auto *ins = pack->instances[ceset[i][j]];
                if (ins != NULL) {
                    if (CENet == NULL)
                        CENet = ins->pins[cePinIdx]->net;
                    else if (ins->pins[cePinIdx]->net != CENet)
                        return false;
                }
            }
        }
        for (int i = 0; i < 2; i++) {
            Net *ClkNet = NULL;
            Net *SRNet = NULL;
            for (int j = 0; j < 8; j++) {
                auto ins = pack->instances[ckset[i][j]];
                if (ins != NULL) {
                    // clk
                    if (ClkNet == NULL)
                        ClkNet = ins->pins[clkPinIdx]->net;
                    else if (ins->pins[clkPinIdx]->net != ClkNet)
                        return false;
                    // sr
                    if (SRNet == NULL)
                        SRNet = ins->pins[srPinIdx]->net;
                    else if (ins->pins[srPinIdx]->net != SRNet)
                        return false;
                }
            }
        }
    }
    return true;
}
bool Database::canMoveInstance(Pack *pack, Instance *rin, int newsitez, int bank) {
    // if (pack->site->x == 56 && pack->site->y == 223) {
    //     cout << rin->name << ',' << rin->x << ',' << rin->y << ',' << rin->z << ',' << rin->packz << endl;
    //     for (auto inst : pack->instances) {
    //         if (inst != NULL) {
    //             cout << inst->name << ',' << inst->x << ',' << inst->y << ',' << inst->z << ',' << inst->packz << endl;
    //         }
    //     }
    // }
    if (rin->master->name == Master::SEQ &&
        !database.isSiteDffValid(pack,rin,bank)) {
        //���Ϸ�
        return false;
    }
    if (rin->IsLUT() && (isEmptyLUTSite(pack->site,rin->bankz) == 8)) return false; // LUT����

    int newz = 0;
    if (rin->master->name == Master::SEQ) {
        newz = database.getSpaceForInstance(pack->site, rin, bank);
    } else {
        if (newsitez == -1) {
            newz = database.getSpaceForInstance(pack->site, rin, bank);
        } else {
            newz = newsitez;
        }
    }
    int npacz = newz;
    if(newz == -1) {
        // ���㹻λ���ƶ�
        return false;
    }
    assert(pack->instances[newz] == NULL);
    if (newz >=16) {
        newz = newz-16;
    } else {
        newz /= 2;
    }
    // cout << rin->name << " to " << pack->site->x <<','<< pack->site->y << " :" << newz << ',' << npacz << endl;
    rin->getIPinLen();
    rin->getOPinlen();
    vector<double> preH = database.getMoveH(rin);
    double prehpwl = preH[0];
    double precritic = preH[1];
    int prex = rin->x;
    int prey = rin->y;
    int prez = rin->z;
    int prebankz = rin->bankz;
    int prepackz = rin->packz;
    // rin->x = pack->site->x;
    // rin->y = pack->site->y;
    // rin->getIPinLen();
    // rin->getOPinlen();
    // vector<double> afH2 = database.getMoveH(rin);
    vector<double> aftH = getMoveChange(rin,pack->site->x,pack->site->y);
    // vector<double> aftH = database.getMoveH(rin);
    double afhpwl = aftH[0];
    double aftcritic = aftH[1];
    // vector<double> aftH2 = getMoveChange(rin,pack->site->x,pack->site->y);
    // double afthpwl2 = aftH2[0];
    // double aftcritic2 = aftH2[1];
    // if (afhpwl != afH2[0] || aftcritic != afH2[1]) {
    //     cout << "error1: move change function" << endl;
    // }
    // cout << prehpwl2 << " , " << afhpwl << endl;
    // cout << prehpwl << " , " << afhpwl << endl;
    // cout << precritic << " , " << aftcritic << endl;
    if (aftcritic > precritic) {
        rin->x = prex;
        rin->y = prey;
        rin->z = prez;
        rin->bankz = prebankz;
        rin->packz = prepackz;
        rin->getIPinLen();
        rin->getOPinlen();
        return false; // �ƶ���ؼ�·����������
    }
    if (afhpwl < prehpwl) {
        database.getSite(prex, prey)->pack->instances[prepackz] = NULL;
        pack->instances[npacz] = rin;
        if (rin->master->name == Master::LUT6) {
            pack->instances[npacz+1] = rin;
        }

        rin->pack = pack;
        rin->x = pack->site->x;
        rin->y = pack->site->y;
        rin->z = newz;
        rin->packz = npacz;
        rin->bankz = bank;
        rin->getIPinLen();
        rin->getOPinlen();
        vector<double> afte3 = database.getMoveH(rin);
        double afterL3 = afte3[0];
        double afterC3 = afte3[1];
        if (afterL3 != afhpwl || afterC3 != aftcritic) {
            cout << "error: move change function" << endl;
        }
        // if (pack->site->x == 56 && pack->site->y == 223) {
        //     cout << rin->name << "(" << Master::NameEnum2String(rin->master->name) << ")"
        //     << " : " << prex << ',' << prey << ',' << prez << ',' << prepackz
        //     << " to " << pack->site->x<<','<< pack->site->y << ',' << newz << ','
        //     << npacz << " :" << afhpwl << ',' << prehpwl << " nice!" << endl;
        //     for (auto inst : pack->instances) {
        //         if (inst != NULL) {
        //             cout << inst->name << ',' << inst->x << ',' << inst->y << ',' << inst->z << ',' << inst->packz << endl;
        //         }
        //     }
        // }
        return true;
    } else {
        rin->x = prex;
        rin->y = prey;
        rin->z = prez;
        rin->bankz = prebankz;
        rin->packz = prepackz;
        rin->getIPinLen();
        rin->getOPinlen();
        return false;
    }
}
int Database::isEmptyDFFSite(Site* site, int bank) {
    assert(site->type->name == SiteType::PLB);
    int dffnum = 0;
    int l = 0, h = 0;
    if (bank == 0) {
        l = 16;
        h = 23;
    } else if (bank == 1) {
        l = 24;
        h = 31;
    } else {
        l = 16;
        h = 31;
    }
    // cout << l <<','<< h << endl;
    for (int i = l; i <= h; i++) {
        if (site->pack->instances[i]!=NULL) {
            // cout << site->pack->instances[i]->name << endl;
            dffnum++;
        }
    }
    return dffnum;
}
int Database::isEmptyLUTSite(Site* site, int bank) {
    assert(site->type->name == SiteType::PLB);
    int lutnum = 0;
    int l = 0, h = 0;
    if (bank == 0) {
        l = 0;
        h = 7;
    } else if (bank == 1) {
        l = 8;
        h = 15;
    }
    for (int i = l; i <= h; i++) {
        if (site->pack->instances[i]!=NULL) {
            lutnum++;
        }
    }
    return lutnum;
}
bool Database::isSiteDffValid(Pack *pack, Instance *ins, int bank) {
    // �������pack��dff�Ƿ�Ϸ���ins�Ƿ��ܹ���pack��dff����
    assert(ins->master->name == Master::SEQ);
    if (isEmptyDFFSite(pack->site,bank)==0) return true; //Ϊ�հ��Ϸ���
    int diffCeNum = 0;
    int numDff = 0;
    int l = 0, h = 0;
    if (bank == 0) {
        l = 16;
        h = 23;
    } else {
        l = 24;
        h = 31;
    }
    Net *othceNet = NULL;
    Net *ceNet = NULL;
    Net *srNet = NULL;
    Net *clkNet = NULL;
    for (int i = l; i <= h; i++) {
        if (pack->instances[i]!=NULL) {
            if (ceNet==NULL) ceNet = pack->instances[i]->pins[database.cePinIdx]->net;
            if (srNet==NULL) srNet = pack->instances[i]->pins[database.srPinIdx]->net;
            if (clkNet==NULL) clkNet = pack->instances[i]->pins[database.clkPinIdx]->net;
            Net *thidceNet = pack->instances[i]->pins[database.cePinIdx]->net;
            if (diffCeNum==0 && ceNet!=thidceNet) {
                othceNet = thidceNet;
            } else if (diffCeNum ==1 && ceNet!=thidceNet && othceNet!=thidceNet){
                return false;
            }
            if (srNet != pack->instances[i]->pins[database.srPinIdx]->net) return false;
            if (clkNet != pack->instances[i]->pins[database.clkPinIdx]->net) return false;
            numDff++;
            // if (ins->name == "inst_105314") {
            //     cout << pack->instances[i]->name << pack->instances[i]->bankz << endl;
            //     if (ceNet!=NULL) {
            //         cout << " ceNet:" << pack->instances[i]->pins[database.cePinIdx]->net->name;
            //     }
            //     if (srNet!=NULL) {
            //         cout << " srNet:" << pack->instances[i]->pins[database.srPinIdx]->net->name;
            //     }
            //     if (clkNet!=NULL) {
            //         cout << " clkNet:" << pack->instances[i]->pins[database.clkPinIdx]->net->name << endl;
            //     }
            // }
        }
    }
    if (ins==NULL) {
        return true;
    } else {

        Net *insceNet = ins->pins[database.cePinIdx]->net;
        Net *inssrNet = ins->pins[database.srPinIdx]->net;
        Net *insclkNet = ins->pins[database.clkPinIdx]->net;
        // if (ins->name == "inst_105314") {
        //     cout << ins->name << ins->bankz << endl;
        //     if (insceNet!=NULL) {
        //         cout << "insceNet:" << ins->pins[database.cePinIdx]->net->name;
        //     }
        //     if (inssrNet!=NULL) {
        //         cout << " inssrNet:" << ins->pins[database.srPinIdx]->net->name;
        //     }
        //     if (insclkNet!=NULL) {
        //         cout << " insclkNet:" << ins->pins[database.clkPinIdx]->net->name;
        //     }
        //     cout << endl;
        // }
        if (insceNet!=ceNet && insceNet!=othceNet) return false;
        if (inssrNet!=srNet) return false;
        if (insclkNet!=clkNet) return false;
        return true;
    }
}
bool Database::isSiteLutValid(Instance* lutl, Instance* lutr) {
    if (lutl->master->name == Master::LUT6 || lutr->master->name == Master::LUT6 ||
        lutl->master->name == Master::LUT6X || lutr->master->name == Master::LUT6X) return false;
    int tot = lutl->pins.size() + lutr->pins.size();
    auto numDupInputs = database.NumDupInputs(*lutl, *lutr);  // reduce routing demand
    auto numDistInputs = tot - 2 - numDupInputs;
    if (numDistInputs > 6) {
        return false;
    }
    return true;
}
bool Database::canMoveSite(Site* site, vector<Instance *> dffinstances) {
    if (site->type->name == SiteType::PLB) {
        int fullnum = 8;
        int ins = database.isEmptyDFFSite(site, dffinstances[0]->bankz);
        if (ins == 0) {
            site->pack = new Pack(site->type,site);
            for (int i = 0; i < dffinstances.size(); i++) {
                if (dffinstances[i]->master->name != Master::LUT6) {
                    site->pack->instances[dffinstances[i]->packz] = dffinstances[i];
                } else {
                    site->pack->instances[dffinstances[i]->packz] = dffinstances[i];
                    site->pack->instances[dffinstances[i]->packz+1] = dffinstances[i];
                }
            }
            return true;
        } else if (ins == fullnum) {
            return false;
        } else {
            // ���site���Ƿ��instance��һ��,�ȿ���dff
            if (dffinstances[0]->master->name == Master::SEQ) {
                for(int i = 0; i < dffinstances.size(); i++) {
                    if (isSiteDffValid(site->pack,dffinstances[i],dffinstances[i]->bankz)) {
                        return false;
                    }
                }
            }

            return true;
        }
    } else {
        return false;
    }
}
int Database::getSpaceForInstance(Site* site, Instance* instance, int bank) {
    if (site->type->name == SiteType::PLB) {
        int fullnum = 8;
        int ins = 0;
        if (instance->master->name == Master::SEQ) {
            ins = database.isEmptyDFFSite(site, bank);
        } else {
            ins = database.isEmptyLUTSite(site, bank);
        }
        int lutl = 0, ffl = 16, dffsite = 16, lutsite = 0;
        // cout << "ins:" << ins << ", bank:" << bank << endl;
        if (bank == 1) {
            lutl = 8;
            ffl = 24;
            dffsite = 24;
            lutsite = 8;
        }
        if (ins == 0) {
            if (instance->master->name == Master::SEQ) {
                return dffsite;
            } else {
                return lutsite;
            }
        } else if (ins == fullnum) {
            return -1;
        } else {
            if (instance->master->name == Master::SEQ) {
                for (int i = 0; i < 8; i++) {
                    if (site->pack->instances[ffl+i] == NULL) {
                        return ffl+i;
                    }
                }
            } else if (instance->master->name == Master::LUT6) {
                for (int i = 0; i < 4; i++) {
                    if (site->pack->instances[lutl+2*i] == NULL&&
                        site->pack->instances[lutl+2*i+1] == NULL) {
                        return lutl+2*i;
                    }
                }
            } else {
                for (int i = 0; i < 4; i++) {
                    if (site->pack->instances[lutl+2*i] == NULL &&
                        site->pack->instances[lutl+2*i+1] == NULL) {
                        return lutl+2*i;
                    } else if (site->pack->instances[lutl+2*i] == NULL &&
                               site->pack->instances[lutl+2*i+1] != NULL &&
                               isSiteLutValid(site->pack->instances[lutl+2*i+1], instance)) {
                        return lutl+2*i;
                    } else if (site->pack->instances[lutl+2*i] != NULL &&
                               site->pack->instances[lutl+2*i+1] == NULL &&
                               isSiteLutValid(site->pack->instances[lutl+2*i], instance)) {
                        return lutl+2*i+1;
                    }
                }
            }
            return -1;
        }
    } else {
        return -1;
    }
}
vector<double> Database::getMoveChange(Instance *ins, int nx, int ny) {
    double afterL = 0;
    double afterCritic = 0;
    for (int i = 0; i < ins->pins.size()-1; i++) {
        Pin *p = ins->pins[i];
        if (p->net == NULL) continue;
        Instance *oinstance = p->net->pins[0]->instance;
        afterL += abs(oinstance->x - nx) + abs(oinstance->y - ny);
        if (p->isTiming) afterCritic += abs(oinstance->x - nx) + abs(oinstance->y - ny);
    }
    if (ins->outPin != NULL) {
        Net *net = ins->outPin->net;
        for (int i = 1; i < net->pins.size(); i++) {
            Instance *instance = net->pins[i]->instance;
            afterL += abs(instance->x - nx) + abs(instance->y - ny);
            if (net->pins[i]->isTiming) afterCritic += abs(instance->x - nx) + abs(instance->y - ny);
        }
    }
    vector<double> aftH;
    aftH.resize(2,0.0);
    aftH[0] = afterL;
    aftH[1] = afterCritic;
    return aftH;
}
vector<double> Database::getMoveH(Instance* instance) {
    vector<double> MoveL;
    MoveL.resize(2,0.0);
    double totalMoveL = 0;
    double criticalL = 0;
    totalMoveL += instance->getTotalH();
    for (int i = 0; i < instance->pins.size()-1; i++) { //inPin
        Pin *p = instance->pins[i];
        if (p->net == NULL) continue;
        if (p->isTiming) {
            // cout << instance->name << " has timing pin:I_" << i << " length:" << instance->IpinLens[i] << endl;
            criticalL += instance->IpinLens[i];
        }
        // Instance *oinstance = p->net->pins[0]->instance;
        // cout << instance->name << ',' << instance->x << ',' << instance->y << ',' << instance->IpinLens[i] << endl;
        // totalMoveL += instance->IpinLens[i];
    }
    if (instance->outPin != NULL) {
        Net *net = instance->outPin->net;
        for (int i = 1; i < net->pins.size(); i++) {
            // Instance *i_instance = net->pins[i]->instance;
            if (net->pins[i]->isTiming) {
                // cout << net->pins[i]->instance->name << " has timing pin:OI_" << i << " length:" << instance->OpinLens[i] << endl;
                criticalL += instance->OpinLens[i];
            }
            // totalMoveL += instance->OpinLens[i];
        }
    }
    // cout << criticalL << endl;
    MoveL[0] = totalMoveL;
    MoveL[1] = criticalL;
    return MoveL;
}


void Database::swapLUT6withNLUT6(Instance* lut6,Instance* nlut6a,Instance* nlut6b){
    lut6->pack->instances[lut6->packz] = nlut6a;
    lut6->pack->instances[lut6->packz+1] = nlut6b;
    nlut6a->pack->instances[nlut6a->packz] = lut6;
    nlut6b->pack->instances[nlut6b->packz] = lut6;
    Pack *tmppack = lut6->pack;
    lut6->pack = nlut6a->pack;
    nlut6a->pack = tmppack;
    nlut6b->pack = tmppack;
    //上面交换pack中的instances以及pack，下面交换inst属性
    int tmpx = nlut6a->x;
    int tmpy = nlut6a->y;
    int tmpz = nlut6a->z;
    int tmppackz = nlut6a->packz;
    int tmpbankz = nlut6a->bankz;
    nlut6a->x = lut6->x;
    nlut6a->y = lut6->y;
    nlut6a->z = lut6->z;
    nlut6b->x = lut6->x;
    nlut6b->y = lut6->y;
    nlut6b->z = lut6->z;
    nlut6a->packz = lut6->packz;
    nlut6b->packz = lut6->packz+1;
    nlut6a->bankz = lut6->bankz;
    nlut6b->bankz = lut6->bankz;
    lut6->x =  tmpx;
    lut6->y = tmpy;
    lut6->z = tmpz;
    lut6->packz = tmppackz;
    lut6->bankz = tmpbankz;


}
void Database::swapLUT6withLUT6(Instance* lut6a,Instance* lut6b){
    lut6a->pack->instances[lut6a->packz] = lut6b;
    lut6a->pack->instances[lut6a->packz+1] = lut6b;
    lut6b->pack->instances[lut6b->packz] = lut6a;
    lut6b->pack->instances[lut6b->packz+1] = lut6a;
    Pack *tmppack = lut6a->pack;
    lut6a->pack = lut6b->pack;
    lut6b->pack = tmppack;
    int tmpx = lut6a->x;
    int tmpy = lut6a->y;
    int tmpz = lut6a->z;
    int tmppackz = lut6a->packz;
    int tmpbankz = lut6a->bankz;
    lut6a->x = lut6b->x;
    lut6a->y = lut6b->y;
    lut6a->z = lut6b->z;
    lut6a->packz = lut6b->packz;
    lut6a->bankz = lut6b->bankz;
    lut6b->x =  tmpx;
    lut6b->y = tmpy;
    lut6b->z = tmpz;
    lut6b->packz = tmppackz;
    lut6b->bankz = tmpbankz;

}


void Database::dplace(){

    //优化方向
     //1、时钟区域优化改成全局优化 check
     //2、LUT6加入考虑 check
     //3、空lut加入考虑
     //4、关键路径加入考虑
     //5、筛选独立集起始位置
     //6、筛选最优独立集



    vector<vector<pair<Instance *,Instance *>>> IndependLUTs ;
    vector<Site*> PLBs;
    PLBs.reserve(100);
    for (int row = 0;row < sitemap_nx;row++) {
        for (int col = 0; col < sitemap_ny; col++) {
            Site *site = this->sites[row][col];
            if (site == nullptr){
                continue;
            }
            if (site->pack == nullptr){
                continue;
            }
            if (site->pack->type == nullptr){
                continue;
            }
            if (site->pack->type->name == NULL){
                continue;
            }
            if (site->pack->type->name == SiteType::PLB) {
                PLBs.push_back(site);
            }
        }
    }
    int mid = PLBs.size()/2;
//            先判断��取的PLB是否为空,或��要不要找内容最多的PLB
    for (int i = 0;i < MAX_TIMES_OF_SELECT_PLB_FOR_BIPARTITE;i++){
        int cntLUT = 0,cntFixed = 0;
        for (int j = 0;j<8;++j){
            if (PLBs[mid]->pack->instances[2*j]!= nullptr||PLBs[mid]->pack->instances[2*j+1]!= nullptr){
                ++cntLUT;
            }
        }
        if (cntLUT<MIN_SIEZE_OF_LUT_FOR_BIPARTITE){
            mid = mid+10*i;
            if (mid > PLBs.size()) {
                cout << "error" << mid << ',' << PLBs.size() << endl;
            }
        }else{
            for (int cnt_lut = 0;cnt_lut<8;cnt_lut++){
                Instance *l1 = PLBs[mid]->pack->instances[2*cnt_lut];
                Instance *l2 = PLBs[mid]->pack->instances[2*cnt_lut+1];
                if (l1 == nullptr || l2 == nullptr) continue;
                cout << l1 << ',' << l2 << endl;
                if (l1->fixed || l2 -> fixed) cntFixed++;
            }
            if (cntFixed > MAX_FIXED) continue;
            else break;
        }

    }
    cout<<"finish select PLB"<<endl;
    for (int i = 0;i < 8;i++){//遍历中间PLB的所有LUT
        vector<pair<Instance *,Instance *>> LUTs;
        Instance *luta = PLBs[mid]->pack->instances[2*i];
        Instance *lutb = PLBs[mid]->pack->instances[2*i+1];
        if (luta!=NULL&&lutb!=NULL) {
            if (luta->master->name > Master::LUT6||lutb->master->name > Master::LUT6) continue;
            if (luta->fixed||lutb->fixed) continue;
        }
        else{
            if (luta == nullptr) {
                luta = new Instance();
                luta->x = PLBs[mid]->x;
                luta->y = PLBs[mid]->y;
                luta->z = i;
                luta->packz = 2 * i;
                luta->bankz = i / 4;
                luta->pack = PLBs[mid]->pack;
            }
            if (lutb == nullptr){
                lutb = new Instance();
                lutb->x = PLBs[mid]->x;
                lutb->y = PLBs[mid]->y;
                lutb->z = i;
                lutb->packz = 2*i+1;
                lutb->bankz = i/4;
                lutb->pack = PLBs[mid]->pack;
            }
        }
        LUTs.push_back(make_pair(luta,lutb));//先把中间的LUT加进厄1�7
        cout<<"finish select LUT"<<endl;
        for (auto s:PLBs){//judge if the LUT is independent
            if (s->x == PLBs[mid]->x && s->y == PLBs[mid]->y) continue;
            for (int j = 0;j < 8;j++){
                Instance *lutc = s->pack->instances[2*j];
                Instance *lutd = s->pack->instances[2*j+1];
                if (lutc!=NULL&&lutd!=NULL){
                    cout <<"lutc and lutd is not null"<<endl;
                    if (lutc->fixed||lutd->fixed) continue;
                    if (lutc->master->name > Master::LUT6||lutd->master->name > Master::LUT6) continue;
                    int isIndepend = 1;
                    for (auto lut:LUTs){//考察lutc和lutd的独立��1�7
                        if (lut.first != lut.second) {
                            if (lutc->master->name == Master::LUT6) {
                                cout<<"lutc is LUT6"<<endl;
                                if(isSameNet(lut.first,lutc)||isSameNet(lut.second,lutc)) {
                                    isIndepend = 0;
                                    break;
                                }
                            }
                            else {
                                if (isSameNet(lut.first,lutc)||isSameNet(lut.first,lutd)||isSameNet(lut.second,lutd)||isSameNet(lut.second,lutc)){
                                    isIndepend = 0;
                                    break;
                                }
                            }
                        } else {
                            if (lutc->master->name == Master::LUT6) {
                                if(isSameNet(lut.first,lutc)) {
                                    isIndepend = 0;
                                    break;
                                }
                            } else {
                                if (isSameNet(lut.first,lutc)||isSameNet(lut.first,lutd)){
                                    isIndepend = 0;
                                    break;
                                }
                            }
                            // cout << 1 << endl;
                        }
                        // cout << lutc->name << ',' << lutd->name << " is independent" << endl;
                    }
                    cout<<"lutc and lutd are independent"<<endl;
                    if (isIndepend) {
                        LUTs.push_back(make_pair(lutc,lutd));
                        cout<<"successful push_back lutc and lutd"<<endl;
                    }
                }
                else{
                    if (lutc == NULL){
                        lutc = new Instance();
                        lutc->x = s->x;
                        lutc->y = s->y;
                        lutc->z = j;
                        lutc->packz = 2*j;
                        lutc->bankz = j/4;
                        lutc->pack = s->pack;
                    }
                    if (lutd = NULL){
                        lutd = new Instance();
                        lutd->x = s->x;
                        lutd->y = s->y;
                        lutd->z = j;
                        lutd->packz = 2*j+1;
                        lutd->bankz = j/4;
                        lutd->pack = s->pack;
                    }
                }
                LUTs.push_back(make_pair(lutc,lutd));
            }
        }
        if (!LUTs.empty()) IndependLUTs.push_back(LUTs);
    }
    cout <<"finish get Independent set"<<endl;
    //上面是寻找最大独立集集合，这段用多线程
    int idx_best_set = 0;
    int min_cost = INT_MAX;
    vector<pair<int, long>> best_res;
    for (auto LUTs:IndependLUTs){
        vector<vector<pair<int, long>>> allCandSites(LUTs.size(),vector<pair<int, long>>(LUTs.size(), make_pair(-1,0)));  // (site, score),这是丢�个无向图
        //初始化这个无向图自己和自己的代价丄1�70
        for (int i = 0;i<LUTs.size();i++){
            allCandSites[i][i] = {i,COST_BASE};
        }
        //为边附权重
        for (int row = 0;row<LUTs.size();row++){
            for (int col = row+1;col<LUTs.size();col++){
                int cost = 0;
                if (LUTs[row].first == nullptr)
                if (LUTs[row].first->master->name == Master::LUT6 && LUTs[col].first->master->name == Master::LUT6){
                    cost += calLUTSwapCost(LUTs[row].first,LUTs[col].first)+COST_BASE;
                }
                else{
                    if (LUTs[row].first->master->name == Master::LUT6){
                        cost += calLUT6wNLUT6Cost(LUTs[row].first,LUTs[col].first,LUTs[col].second)+COST_BASE;
                    }
                    else{
                        if (LUTs[col].first->master->name == Master::LUT6){
                            cost += calLUT6wNLUT6Cost(LUTs[col].first,LUTs[row].first,LUTs[row].second)+COST_BASE;
                        }
                        else{
                            cost += calLUTSwapCost(LUTs[row].first,LUTs[col].first)+ calLUTSwapCost(LUTs[row].second,LUTs[col].second)+COST_BASE;
                        }
                    }
                }
                allCandSites[row][col] = {col,cost};
                allCandSites[col][row] = {row,cost};
            }
        }
        vector<pair<int, long>> res;
        long cost = 0;
        MinCostBipartiteMatching(allCandSites, allCandSites.size(), allCandSites.size(), res, cost);
        if (cost < min_cost){
            min_cost = cost;
            idx_best_set = idx_best_set;
            best_res = res;
        }
    }
    cout<<"finish bipartite matching"<<endl;
    //接下来要对二分匹配的结果进行判断和��择是否录用
    bool isSuccessfulBipartite = true;
    vector<pair<Instance *,Instance *>> LUTs = IndependLUTs[idx_best_set];
    vector<pair<pair<Instance * ,Instance * >, pair<Instance * ,Instance * >>> pairs;
    for (size_t pid = 0; pid < LUTs.size(); ++pid) {
        if (best_res[pid].first < 0) {
            isSuccessfulBipartite = false;
            pairs.clear();
            break;
        } else {//目前棢�查到的匹配成功，加入候��1�7
            auto LUTPaira = LUTs[pid];
            auto LUTPairb = LUTs[best_res[pid].first];
            if (LUTPaira.first->x != LUTPairb.first->x && LUTPaira.first->y != LUTPairb.first->y) {
                pairs.push_back({LUTPaira, LUTPairb});
            }
        }
    }
    if (isSuccessfulBipartite) {
        for (auto pair : pairs) {
            if (pair.first.first->master->name == Master::LUT6 && pair.second.first->master->name == Master::LUT6){
                swapLUT6withLUT6(pair.first.first, pair.second.first);
            }
            else{
                if (pair.first.first->master->name == Master::LUT6){
                    swapLUT6withNLUT6(pair.first.first,pair.second.first,pair.second.second);
                }
                else{
                    if (pair.second.first->master->name == Master::LUT6){
                        swapLUT6withNLUT6(pair.second.first,pair.first.first,pair.first.second);
                    }
                    else{
                        SwapInstMap(pair.first.first, pair.second.first);
                        SwapInstMap(pair.first.second, pair.second.second);
                    }
                }
            }
        }
            cout<<"bipartite succesful"<<endl;
    }
    else{
        cout<<"bipartite failed"<<endl;
    }
}
//int Database::SecondSelectPLBforIndependentSet (int size) {
//    return size/2;
//}
bool Database::isSameNet(Instance * a,Instance * b){
    // for (auto inst:a->ConnInsts) {
    //     cout << a->name << ':' << inst->name << endl;
    // }
    // cout << a->ConnInsts.size() << endl;
    // cout << a->name << '|' << b->name << endl;
    for (auto inst:a->ConnInsts){
        // cout << shared_ptr<Instance>(inst);
        // shared_ptr<Instance> new_inst(inst);
        // cout << inst << ',' << new_inst << ',' << new_inst->name << endl;
        if (inst->name==b->name){
            // cout << inst << " r, " << inst->name << " r, " << b->name << endl;
            return true;
        }
        // cout << inst << ',' << inst->name << ',' << b->name << endl;
    }
    // cout << "is over" << endl;
    return false;
}
int Database::calLUTSwapCost(Instance *a,Instance *b){
    int cost = 0;
    int x1 = a->x;
    int y1 = a->y;
    int x2 = b->x;
    int y2 = b->y;
    vector<int> oriDist1;
    vector<int> oriDist2;
    vector<int> aftDist1;
    vector<int> aftDist2;
    if (a->id != -1){
        for (auto inst:a->ConnInsts){
            int px1 = inst->x;
            int py1 = inst->y;
            oriDist1.push_back(calMhatton(x1,y1,px1,py1));
            aftDist1.push_back(calMhatton(x2,y2,px1,py1));
        }
        for (int i = 0;i<oriDist1.size();i++){
            cost += aftDist1[i]-oriDist1[i];
        }
    }
    if (b->id != -1){
        for (auto inst:b->ConnInsts){
            int px2 = inst->x;
            int py2 = inst->y;
            oriDist2.push_back(calMhatton(x2,y2,px2,py2));
            aftDist2.push_back(calMhatton(x1,y1,px2,py2));
        }

        for (int i = 0;i<oriDist2.size();i++){
            cost += aftDist2[i]-oriDist2[i];
        }
    }

    return cost;

}
int Database::calLUT6wNLUT6Cost(Instance *lut6,Instance *nlut6a,Instance *nlut6b){
    int cost = 0;
    cost += calLUTSwapCost(lut6,nlut6a);
    if (nlut6b ->id !=-1){
        int x1 = lut6->x;
        int y1 = lut6->y;
        int x2 = nlut6b->x;
        int y2 = nlut6b->y;
        vector<int> oriDist;
        vector<int> aftDist;
        for (auto inst:nlut6b->ConnInsts){
            int px2 = inst->x;
            int py2 = inst->y;
            oriDist.push_back(calMhatton(x2,y2,px2,py2));
            aftDist.push_back(calMhatton(x1,y1,px2,py2));
        }
        for (int i = 0;i<oriDist.size();i++){
            cost += aftDist[i]-oriDist[i];
        }
    }
    return cost;
}

void Database::SwapInstMap(Instance *a,Instance *b){
    //交换两个instance的位罄1�7
    //交换pack、packz和xyz就行
    //先交换pack的instances
        Instance* tmpidx_Inst = a;
        a->pack->instances[a->packz] = b;
        b->pack->instances[b->packz] = tmpidx_Inst;
        Pack *tmppack = a->pack;
        a->pack = b->pack;
        b->pack = tmppack;
        int tmpx = a->x;
        int tmpy = a->y;
        int tmpz = a->z;
        a->x = b->x;
        a->y = b->y;
        a->z = b->z;
        b->x = tmpx;
        b->y = tmpy;
        b->z = tmpz;
        int tmppackz = a->packz;
        a->packz = b->packz;
        b->packz = tmppackz;
        int tmpbankz = a->bankz;
        a->bankz = b->bankz;
        b->bankz = a->bankz;
        //上面交换inst，下面交换pack




}
int Database::calMhatton(int x1,int y1,int x2,int y2){
    return abs(x1-x2)+abs(y1-y2);
}
bool Database:: isPlacementValid() {
    for (int i = 0; i < (int)sites.size(); i++) {
        for (int j = 0; j < (int)sites[i].size(); j++) {
            if (sites[i][j]==NULL) continue;
            if (sites[i][j]->pack != NULL && sites[i][j]->pack->site != sites[i][j]) {
                printf( "error in site->pack->site consistency validation");
                return false;
            }
        }
    }
    for (int i = 0; i < (int)packs.size(); i++) {
        if (packs[i]->site->pack != packs[i]) {
            printf( "error in pack->site->pack consistency validation");
            return false;
        }
    }
//    for (int i = 0; i < (int)instances.size(); i++) {
//        // cout<<instances[i]->id<<endl;
//        if (instances[i]->pack->instances[instances[i]->slot] != instances[i]) {
//            printf( "error in instance->pack->instance consistency validation");
//            return false;
//        }
//    }

    for (int i = 0; i < (int)packs.size(); i++) {
        for (int j = 0; j < (int)packs[i]->instances.size(); j++) {
            if (packs[i]->instances[j] != NULL && packs[i]->instances[j]->pack != packs[i]) {
                printf( "error in pack->instance->pack consistency validation");
                return false;
            }
        }
    }
    for (int i = 0; i < (int)sites.size(); i++) {
        for (int j = 0; j < (int)sites[i].size(); j++) {
            if (sites[i][j]==NULL) continue;
            if (sites[i][j]->pack != NULL && !isPackValid(sites[i][j]->pack)) {
                cout<<"site"<<i<<" "<<j<<"its pack is not valid"<<endl;
                printf( "error in site legality validation");
                return false;
            }
        }
    }
    for (int i = 0; i < (int)instances.size(); i++) {
        if (!isPackValid(instances[i]->pack)) {
            printf( "error in instance legality validation");
            return false;
        }
    }
    //上面这些丢�致��判断可以照搄1�7
    for (int i = 1; i < (int)sites.size(); i++) {
        for (int j = 1; j < (int)sites[i].size(); j++) {
            if (sites[i][j]==NULL) continue;
            if (sites[i][j]->pack != NULL && !isPackValid(sites[i][j]->pack)) {
                printf( "error in site legality validation");
                return false;
            }
        }
    }
    for (int i = 1; i < (int)instances.size(); i++) {
        if (!isPackValid(instances[i]->pack)) {
            printf( "error in instance legality validation");
            return false;
        }
    }
    //上面感觉好像重复遍历了，每个pack应该都在site里面亄1�7
    if (crmap_nx == 0) return true;//clock region map的水平方向长庄1�7

    int  nOFCr = 0;
    bool isClkLeg = true;

    // check clkrgn

    int cnt_clk = 0;
    for (auto net : nets) {
        if (net->isClk) {
            ++cnt_clk;
            //找到输出引脚，找到其对应inst，找到其site，计敄1�7
            for (auto p:net->pins){
                if (p->type->type == "OUTPUT"){
                    int x = p->instance->x;
                    int y = p->instance->y;
                    for (int i = 0;i<5;i++){
                        if (x>=clkidx_row[i]&&x<clkidx_row[i+1]){
                            for (int j = 0;j<5;j++){
                                if (y>=clkidx_col[j]&&y<clkidx_col[j+1]){
                                    clkrgns[i][j]->clknets.insert(net);
                                }
                            }
                        }
                    }
                }
            }

        }
    }
    cout<<"cnt_clk:"<<cnt_clk<<endl;


    for (int x = 0; x < crmap_nx; x++) {
        for (int y = 0; y < crmap_ny; y++) {
            if (clkrgns[x][y]->clknets.size() > 28) {//这里好像计算规则丢�栄1�7
                cout<<"error in clkrgn_X"<<x<<"_Y"<<y<<" lx:"<<clkrgns[x][y]->lx<<" ly:"<<clkrgns[x][y]->ly<<" hx:"<<clkrgns[x][y]->hx
                    <<" hy:"<<clkrgns[x][y]->hy<<" clknets.size():"<<clkrgns[x][y]->clknets.size()<<endl;
                isClkLeg = false;
            }
            cout <<"clkrgn insert is valid:"<<clkrgns[x][y]->clknets.size()<<endl;
        }
    }
    if (!isClkLeg) {
        return false;
    }

    return true;
}
void Database::recInstConn(){
    for (auto n:database.nets){
        Instance *inst = n->pins[0]->instance;
        for (auto p:n->pins){
            if (p->type->type == "OUTPUT"){
                continue;
            }
            else{
                inst->ConnInsts.push_back(p->instance);
                p->instance->ConnInsts.push_back(inst);
            }
        }
    }
}
