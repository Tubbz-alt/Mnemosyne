#include <iostream>
#include <fstream>
#include <stdint.h>
#include <vector>

#include <boost/filesystem.hpp>

#include "Ressource.h"
#include "Revision.h"
using namespace std;
using namespace Athena::Mnemosyne;



int main(){
    string str;
    ifstream s("/home/severus/Desktop/1.html",ios::in);
    char c;
    while( s.get(c) )
        str += c;

    Ressource r;
    RessourceHandler rHandler;
    RevisionHandler revHandler;
//
//        r.getRevision()->setIStream( "/home/severus/Desktop/1.stream");
//        r.getRevision()->setOStream( "/home/severus/Desktop/1.stream");
    vector<uint64_t> ids;
    ids.push_back(123);
    r.setChunkIds( ids );


///Creation ressource
//
    rHandler.newRevision(&r, str);


///lecture


//    r.setCurrentRevision(3);
//    ofstream test("/home/severus/Desktop/2.html");
//    string a=rHandler.buildRevision(r, 1);
//    test<<a;

    return 0;
}
