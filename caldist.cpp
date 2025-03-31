//
// Created by s on 2024/11/4.
//
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
int Database::calLUTSwapCost(Instance *a,Instance *b){
    //交换两个LUT的位置，计算代价
    int cost = 0;
    int x1 = a->x;
    int y1 = a->y;
    int x2 = b->x;
    int y2 = b->y;
    /* 遍历inst的所有pin就行
     * 1、计算原来的距离
     * 2、计算交换后的距离
     * 3、计算交换后PLB的pin密度
     * 4、有个问题：用二分匹配的话，移动后的代价是总线长还是局部
     */
    vector<int> oriDist1;
    vector<int> oriDist2;
    vector<int> aftDist1;
    vector<int> aftDist2;
    for (auto inst:a->ConnInsts){
        int px1 = inst->x;
        int py1 = inst->y;
        oriDist1.push_back(calMhatton(x1,y1,px1,py1));
        aftDist1.push_back(calMhatton(x2,y2,px1,py1));
    }
    for (auto inst:b->ConnInsts){
        int px2 = inst->x;
        int py2 = inst->y;
        oriDist2.push_back(calMhatton(x2,y2,px2,py2));
        aftDist2.push_back(calMhatton(x1,y1,px2,py2));
    }
    for (int i = 0;i<oriDist1.size();i++){
        cost += aftDist1[i]-oriDist1[i];
    }
    for (int i = 0;i<oriDist2.size();i++){
        cost += aftDist2[i]-oriDist2[i];
    }
    return cost;
}