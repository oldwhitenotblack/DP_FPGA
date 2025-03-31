#ifndef DATABASE_H_
#define DATABASE_H_
#include "..\include\global.h"
#include "alg/bipartite.h"
namespace db {
class Instance;
class Master;
class Net;
class ClkRgn;
class Resource;
class SiteType;
class Net;
class Pack;
class PinType;
class Pin;
class HfCol;
class Site;
class Group;
class Resource {
public:
    enum Name { LUT6, DFF, CARRY4, DRAM, DSP, RAMA, 
                    RAMB, GCLK, IOA, IOB, IPPIN, PLB};
    static Name NameString2Enum(const string &name);
    static string NameEnum2String(const Resource::Name &name);

    Name name;
    SiteType *siteType;
    vector<Master *> masters;

    Resource(Name n);
    Resource(const Resource &resource);

    void addMaster(Master *master);
};
//sitetype��Ӧ��cpp�Ѿ������ˡ�
class SiteType {
public:
    enum Name { DSP, RAMB, RAMA, IO, IOA, IOB, GCLK, PLB, IPPIN};
    static Name NameString2Enum(const string &name);
    static string NameEnum2String(const SiteType::Name &name);

    int id;
    int weight;
    int height;
    Name name;
    vector<Resource *> resources;
    vector<Master *> masters;
    SiteType(Name n);
    SiteType(const SiteType &sitetype);

    void addResource(Resource *resource);
    void addResource(Resource *resource, int count);
    void addMaster(Master *master);
    void addMaster(Master *master, int count);
    bool matchInstance(Instance *instance);
};
//site��Ӧ��cpp�Ѿ������ˡ�
class Site {
public:
    SiteType *type;
    Pack *pack;
    Site *formuti;
    int x, y;
    int w, h;
    Site();
    Site(int x, int y, SiteType *sitetype);
    // center
    inline double cx() { return x + w; }
    inline double cy() { return y + h; }
};

class Pack {
public:
    SiteType *type;
    Site *site;
    vector<Instance *> instances;
    int id;
    void print();
    bool IsEmpty();
    void addInstance(Instance *instance);
    Pack();
    Pack(SiteType *type, Site *site);
};

// TODO: rename to CellType
class Master {
public:
    enum Name {
        LUT1,
        LUT2,
        LUT3,
        LUT4,
        LUT5,
        LUT6,      // LUT
        LUT6X,
        CARRY4,    // CARRY
        DRAM,  // RAM
        DSP,
        F7MUX,
        F8MUX,
        GCLK,
        IOA,
        IOB,
        IPPIN,
        PLB,
        RAMA,
        RAMB,
        SEQ
    };
    static Name NameString2Enum(const string &name);
    static string NameEnum2String(const Master::Name &name);
    int occupyResourceNum;
    Name name;
    Resource *resource;
    SiteType *siteType;
    vector<PinType *> pins;
    
    Master(Name n);
    Master(const Master &master);
    ~Master();
    bool addResource(Resource *resource);
    void addPin(PinType &pin);
    PinType *getPin(const string &name);
};

// TODO: rename to Cell
class Instance {
public:
    int id;
    string name;
    Master *master;
    Pack *pack;
    int slot;
    int x,y,z;
    int packz;
    int bankz;
    int lutOPinIdx;
    Pin *outPin = NULL;
// LUT pin index
    bool fixed;
    bool inputFixed;
    vector<Pin *> pins;
    vector<Instance *> ConnInsts;
    vector<double> IpinLens;                         // ip length of net
    vector<double> OpinLens;                         // op length of net
    unordered_map<string, int> name_instances;

    Instance();
    Instance(const string &name, Master *master);
    Instance(const Instance &instance);
    ~Instance();
    // ���Ӧ����һ����׼��Ԫӳ�䵽�߼���Ԫ���жϣ���Ϊ���ͬ������ʱ��С�ġ�
    inline bool IsLUT() { return master->resource->name == Resource::LUT6; }
    inline bool IsFF() { return master->name == Master::SEQ; }
    inline bool IsCARRY() { return master->name == Master::CARRY4; }
    inline bool IsLUTFF() { return IsLUT() || IsFF(); }  // TODO: check CARRY8
    inline bool IsDSP() { return master->name == Master::DSP; }
    inline bool IsRAMB() { return master->name == Master::RAMB; }
    inline bool IsRAMA() { return master->name == Master::RAMB; }
    // inline bool IsDSPRAM() { return IsDSP() || IsRAM(); }
    inline bool IsIO() { return master->resource->name == Resource::IOA||master->resource->name == Resource::IOB; }

    Pin *getPin(const string &name);
    bool matchSiteType(SiteType *type);
    double getTotalH();
    void getIPinLen();
    void getOPinlen();

};

class PinType {
public:
    string name;
    string type;
    string arr;
    bool isTiming;
    /*
       I = primary input
       O = primary output
       i = input
       o = output
       c = clock
       e = control(ctrl)
       r = reset
    */
    PinType();
    PinType(const string &name, string typ, string arr);
    PinType(const PinType &pintype);
};

class Pin {
public:
    Instance *instance;
    Net *net;
    PinType *type;
    bool isTiming;


    Pin();
    Pin(Instance *instance, int i);
    Pin(Instance *instance, const string &pin);
    Pin(const Pin &pin);
};
/////////////////
class Net {
public:
    int id;
    std::string name;
    std::vector<Pin *> pins;
    bool isClk;

    Net();
    Net(const string &name);
    Net(const Net &net);
    ~Net();

    void addPin(Pin *pin);
};
class ClkRgn {
public:
    string name;
    int lx, ly, hx, hy;
    int cr_x, cr_y;

    std::vector<Site*> sites;
    vector<shared_ptr<Site>> smartsites;

    unordered_set<Net*> clknets;
    vector<vector<HfCol*>> hfcols;

    ClkRgn(string _name, int _lx, int _ly, int _hx, int _hy, int _cr_x, int _cr_y);
    void Init();

    
};
class HfCol {
public:
    int x, ly, hy;
    vector<Site*> sites;
    unordered_set<Net*> clknets;

    ClkRgn* clkrgn;

    HfCol(int _x, int _lx, int _hy, ClkRgn* _clkrgn);
    void Init();
};
class Group {
public:
    int id;
    vector<Instance*> instances;
    double x, y,z;
    double lastX, lastY;
    double areaScale;
    Group()
        : id(-1), x(0.0), y(0.0), z(0.0), lastX(0.0), lastY(0.0), areaScale(1.0) {}
    // void Print();
    bool IsBLE();
    bool IsTypeMatch(Site* site) const;
    // bool InClkBox(Site* site) const;
    SiteType::Name GetSiteType() const;

    // static void WriteGroups(const vector<Group>& groups, const string& fileNamePreFix);
    // static void ReadGroups(vector<Group>& groups, const string& fileNamePreFix);
};
class Database {
private:
    // ��Щmap����Ϊ�˱����ظ����������õģ�ͨ����Ԫ����ѯ�Ƿ��и��౻����
    // net
    unordered_map<string, Net*> name_nets;

    // instance
    unordered_map<Master::Name, Master*, hash<int>> name_masters;
    unordered_map<string, Instance*> name_instances;

    // site
    unordered_map<Resource::Name, Resource*, hash<int>> name_resources;
    unordered_map<SiteType::Name, SiteType*, hash<int>> name_sitetypes;
public:
    Database();
    ~Database();
    void setup();
    
    Master * getMaster(Master::Name name);
    Instance *getInstance(const string &name);
    Net *getNet(const string &name);
    Resource *getResource(Resource::Name name);
    SiteType *getSiteType(SiteType::Name name);
    Master *addMaster(const Master &master);
    Instance *addInstance(const Instance &instance);
    Net *addNet(const Net &net);
    Resource *addResource(const Resource &resource);
    SiteType *addSiteType(const SiteType &sitetype);
    Site *addSite(int x, int y, SiteType *sitetype);
    Site *getSite(int x, int y);
    void setSiteMap(int nx, int ny);
    Instance *getInstance(int x, int y, int z);
    
    // utils
    int NumDupInputs(const Instance& lhs, const Instance& rhs);
    bool IsLUTCompatible(const Instance& lhs, const Instance& rhs);


    // net
    vector<Net*> nets;

    // instance
    vector<Master*> masters;
    vector<Instance*> instances;

    // site
    vector<Resource*> resources;
    vector<SiteType*> sitetypes;
    vector<Pack*> packs;
    vector<vector<Site*>> sites;
    vector<vector<shared_ptr<Site>>> smartsites;


    // clkrgn
    vector<vector<ClkRgn*>> clkrgns;
    vector<vector<shared_ptr<ClkRgn>>> smartclkrgns;

    vector<vector<HfCol*>> hfcols;
    int sitemap_nx;
    int sitemap_ny;
    int crmap_nx;
    int crmap_ny;

    vector<vector<double>> targetDensity;
    int tdmap_nx;
    int tdmap_ny;

    int clkPinIdx, srPinIdx, cePinIdx, ffIPinIdx;  // FF pin index

    void setCellPinIndex();
    //shj add
    int *clkidx_row;
    int *clkidx_col;
    bool isPlacementValid();
    void recInstConn();
    bool isPackValid(Pack* pack);
    int calLUTSwapCost(Instance *a,Instance *b);
    int calLUT2EmptySiteCost(Instance *a,Site *b);
    vector<vector<pair<shared_ptr<Instance>,shared_ptr<Instance>>>> getIndependentLUTs(shared_ptr<ClkRgn> clkrgn);
    void SwapInstMap(Instance *a,Instance *b);
    bool isSameNet(shared_ptr<Instance> a,shared_ptr<Instance> b);
    void dplace();
    void refineCLKrgn();
    void refineDBsites();
    //wjy add
    bool canMoveInstance(Pack *pack, Instance *rin, int newsitez, int bank);
    int isEmptyDFFSite(Site* site, int bank);
    int isEmptyLUTSite(Site* site, int bank);
    bool canMoveSite(Site* site, vector<Instance *> instances);
    int getSpaceForInstance(Site* site, Instance* ins, int bank);
    vector<double> getMoveH(Instance *ins);
    bool isSiteDffValid(Pack *pack, Instance *ins, int bank);
    bool isSiteLutValid(Instance* lutl, Instance* lutr);

    vector<vector<pair<shared_ptr<Instance>,shared_ptr<Instance>>>> test();


    };
extern Database database;
    
}
#endif