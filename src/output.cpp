//
// Created by s on 2024/11/4.
//
#include "output.h"
#include "Database.h"
#include <fstream>
#include <string>
using namespace db;
void output(std::string filename){
    std::ofstream file(filename);
    for (auto inst:database.instances) {
        file << "X" <<inst->x << "Y" <<inst->y <<"Z" <<inst->z << " "
             << inst->master->NameEnum2String(inst->master->name)<< " "
             << inst->name << " ";
        if (inst -> fixed == true) file << "FIXED" << std::endl;
        else file << endl;
    }
    file.close();

}