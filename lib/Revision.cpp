#include "Revision.h"


namespace Athena{
    namespace Mnemosyne{

        /**
         *  Revision
         */
            Revision::~Revision(){
                for(int i=0; i<children.size(); i++)
                    delete children[i];
                if( iStream != NULL )
                    delete iStream;
                if( oStream != NULL )
                    delete oStream;
            }

            list< Revision* > Revision::getParents(){
                if( n != -1 ){
                    list< Revision* > parents = parent->getParents();
                    parents.push_back( this );
                }else
                    return list< Revision* >();
            }

        /**
         *  Revision Handler
         */

            RevisionHandler::RevisionHandler(){}

            RevisionHandler::~RevisionHandler(){}

            uint32_t RevisionHandler::extractSizeTable( ifstream& stream ){
                uint16_t number = 0;
                vector< uint8_t > table;
                char c;
                stream.seekg(-2, stream.end );//revision number is a uint16_t

                stream.get(c);
                number = uint16_t(c);
                number << 8;

                stream.get(c);
                number +=  uint16_t(c);

                return uint32_t(number) * Revision::REVISION_SIZE_TABLE;
            }

            vector<char> RevisionHandler::extractTable(ifstream& stream){
                uint32_t sizeTable = extractSizeTable( stream );
                uint32_t beginningTable = -1 * ( sizeTable +2);
                vector< char > table;
                char c;

                for( uint32_t i=0; i<sizeTable; i++){
                    stream.get(c);
                    table.push_back( c );
                }

                stream.seekg (-2, stream.beg );
                return table;
            }

            uint16_t RevisionHandler::getOrigine( vector<char>& table, vector<char>::iterator& it ){
                uint16_t n=0b0;
                for(int i=0; i<2; i++){
                   bitset<8> ca( *it );
                   for(int j =7 ; j>-1; j--){
                        n=n<<1;
                        n+=ca[j];

                    }
                    it++;
                }
                return n;
            }

            uint64_t RevisionHandler::getIdBeginning( vector<char>& table, vector<char>::iterator& it ){
                uint64_t n=0b0;
                for(int i=0; i<8; i++){
                   bitset<8> ca( *it );
                   for(int j =7 ; j>-1; j--){
                        n=n<<1;
                        n+=ca[j];

                    }
                    it++;
                }
                return n;
            }

            uint64_t RevisionHandler::getSize( vector<char>& table, vector<char>::iterator& it ){
                uint64_t n=0b0;
                for(int i=0; i<8; i++){
                   bitset<8> ca( *it );
                   for(int j =7 ; j>-1; j--){
                        n=n<<1;
                        n+=ca[j];

                    }
                    it++;
                }
                return n;
            }

            uint16_t RevisionHandler::getDiff( vector<char>& table, vector<char>::iterator& it ){
                uint16_t n=0b0;
                for(int i=0; i<2; i++){
                   bitset<8> ca( *it );
                   for(int j =7 ; j>-1; j--){
                        n=n<<1;
                        n+=ca[j];

                    }
                    it++;
                }
            }

            vector<int> RevisionHandler::extractChildren( vector< int >& origines, int parent ){
                vector<int> children;
                for(int i=0; i<origines.size(); i++)
                    if( parent == origines[i] )
                        children.push_back( i );
                return children;
            }

            void RevisionHandler::buildChildren( vector<int>& origines, vector< Revision* > revisions, Revision* current){
                vector<int> children = extractChildren(origines, current->getN());
                Revision* currentChild;
                for(int i=0; i<children.size(); i++){
                    currentChild = revisions[ children[i] ];

                    currentChild->setParent( current );
                    if( currentChild->getN() > 0 )
                        currentChild->setPrevious( revisions[ currentChild->getN()-1 ] );
                    else
                        currentChild->setPrevious( current ); //Current == root

                    if( currentChild->getN() < children.size()-1 ){
                        currentChild->setNext( revisions[ currentChild->getN()+1 ] );
                        currentChild->setLast(  revisions[ children.size()-1 ] );
                    }


                    buildChildren( origines, revisions,  currentChild );
                    current->addChild( currentChild );
                }
            }

            Revision* RevisionHandler::buildStructure( vector<char>& table ){
                vector<char>::iterator it = table.begin();
                if( it == table.end() )
                    return NULL;

                vector< int > origines;
                vector< Revision* > revisions; // pair< begin, size >

                uint64_t tmpBegin;
                uint64_t tmpSize;
                uint16_t tmpDiff;
                while( it != table.end() ){
                    tmpBegin = getIdBeginning(table, it);
                    tmpSize  = getSize(table, it);
                    tmpDiff  = getDiff(table,it);
                    origines.push_back( getOrigine(table, it) );
                    revisions.push_back( new Revision(revisions.size(), tmpBegin, tmpSize, tmpDiff) );
                }

                Revision* root=new Revision(-1);
                buildChildren( origines, revisions, root);
                root;
            }

            Mutation RevisionHandler::readMutation( ifstream& stream ){
                char c; bitset<8> buffer;
                uint8_t type(0);
                uint64_t idBegining(0);
                uint64_t size(0);

                stream.get( c );
                type = uint8_t(c);

                //idBegining
                for(int i=0; i<8; i++){//8bytes <=>uint64_t
                    stream.get( c );
                    idBegining+=c;
                    idBegining << 8;
                }

                //size
                for(int i=0; i<8; i++){//8bytes <=>uint64_t
                    stream.get( c );
                    size+=c;
                    size << 8;
                }

                Mutation m( type, idBegining, size);
                return m;
            }

            void RevisionHandler::applyMutations( vector<char>& data, Revision* rev){
                ifstream* stream = (rev->getIStream());
                Mutation m;
                uint64_t i =0;
                stream->seekg( rev->getIdBeginning() - rev->getRelativeO(), stream->beg );


                while( i < rev->getSize() ){
                    Mutation m=readMutation( *stream );
                    m.apply(data, *stream);
                    i++;
                }
            }

            void RevisionHandler::writeTable( vector<char>& table, ofstream& stream){
                stream.seekp( -1, stream.end );
                for( uint64_t i=0; i<table.size(); i++){
                    stream<<table[i];
                }
            }

            void RevisionHandler::write( vector<char>& data, uint64_t pos, uint64_t length, ofstream& stream){
                for(uint64_t j=pos; j<length; j++)
                    stream<<data[j];
            }

            void RevisionHandler::addTableElement( vector<char> table, uint64_t id, uint64_t size, uint16_t diff, uint16_t o){
                ///ID
                bitset<64> b1(id);
                for(int i=7; i>-1; i--){
                    char t=0;
                    for(int j=7; j>-1; j--){
                        t=t<<1;
                        t+=b1[i*8+j];
                    }
                    table.push_back(t);
                }

                ///Size
                bitset<64> b2(size);
                for(int i=7; i>-1; i--){
                    char t=0;
                    for(int j=7; j>-1; j--){
                        t=t<<1;
                        t+=b2[i*8+j];
                    }
                    table.push_back(t);
                }

                ///Diff
                bitset<16> b3(diff);
                for(int i=1; i>-1; i--){
                    char t=0;
                    for(int j=7; j>-1; j--){
                        t=t<<1;
                        t+=b3[i*8+j];
                    }
                    table.push_back(t);
                }

                ///Diff
                bitset<16> b4(o);
                for(int i=1; i>-1; i--){
                    char t=0;
                    for(int j=7; j>-1; j--){
                        t=t<<1;
                        t+=b4[i*8+j];
                    }
                    table.push_back(t);
                }
            }

            uint64_t RevisionHandler::diff( vector<char>& origine, vector<char>& data ){
                uint64_t num= min( origine.size(), data.size() );
                uint64_t diff = max( origine.size(), data.size() ) - num;

                for(uint64_t i=0; i<num; i++){
                    if( origine[i] != data[i] )
                        diff++;
                }
                return diff;
            }

            vector< uint64_t > RevisionHandler::calculDifferences( Revision* rev,  vector<char>& data ){
                vector< char > tmpData(data);
                vector< uint64_t > differences;
                Revision* tmp = (rev->getRoot()); //Diff with racine <=> rev origine

                while( tmp != NULL ){
                    applyMutations( tmpData, tmp);
                    differences.push_back( diff(tmpData, data) );
                    tmp = tmp->getNext();
                }
                return differences;
            }

            Revision* RevisionHandler::bestOrigin( Revision* rev,  vector<char>& data ){
                if( rev->getN() == -1 )//root
                    return rev;

                vector< uint64_t > differences = calculDifferences( rev, data);
                uint64_t tmpDiff=differences[0];
                Revision* tmpRev = rev->getRoot();

                for( int i=1; i<differences.size(); i++){
                    if( tmpDiff > differences[i] ){
                        tmpDiff = differences[i];
                        tmpRev    = tmpRev->getNext();
                    }
                }
                return tmpRev;
            }

            void RevisionHandler::createMutations( vector<char>& origine, vector<char>& data, ofstream& stream, uint64_t pos){
                stream.seekp(pos, stream.end);

                //Update first
                uint64_t i(0);
                uint64_t n = min( origine.size(), data.size() );

                uint64_t length(0);
                while( i<n ){
                    if( origine[i] !=  data[i] ){
                        length++;
                    }else if(length != 0){
                        stream<<Mutation::UPDATE << (i-length) << length; //type(uint8_t)beginning(uint64_t)size(uint64_t)
                        write(data, i-length, length, stream);
                        length=0;
                    }
                    i++;
                }

                //Delete
                if( origine.size()>data.size() )
                    stream<<Mutation::DELETE << data.size() << origine.size() - data.size(); //type(uint8_t)beginning(uint64_t)size(uint64_t)

                //Insert
                if( origine.size()<data.size() ){
                    stream<<Mutation::INSERT << origine.size() << data.size() - origine.size();
                    write(data, origine.size(), data.size()-origine.size(), stream);
                }
            }

            Revision* RevisionHandler::newRevision( Revision* rev,  vector<char>&newData){
                rev = rev->getLast();

                ChunkHandler* cHandler;
                Revision* origin = bestOrigin( rev, newData );
                uint32_t tableSize = extractSizeTable( *rev->getIStream() );
                vector<char> table = extractTable( *rev->getIStream() );

                ///Building of origin
                vector<char> tmpData;
                list< Revision* > parents = origin->getParents();
                for( list< Revision* >::iterator it = parents.begin() ; it!=parents.end() ; it++ )
                    applyMutations( tmpData, *it);

                applyMutations( tmpData, origin); //Data is now hydrate

                ///Rev creation, size will be hydrate later
                Revision* newRev= new Revision( rev->getN()+1, rev->getIdBeginning()+rev->getSize(), 0, diff( tmpData, newData) );
                rev->addChild( newRev );
                rev->setPrevious( rev );
                rev->setParent( origin );
                rev->setRoot( rev->getRoot() );
                newRev->setIStream( rev->getIStream() );
                newRev->setOStream( rev->getOStream() );

                createMutations( tmpData, newData, *(newRev->getOStream()), tableSize);

                ///Size
                rev->getIStream()->seekg (0, rev->getIStream()->end);
                newRev->setSize( rev->getIStream()->tellg() - rev->getIdBeginning()+rev->getSize() );

                ///Maj of the table
                addTableElement( table, newRev->getIdBeginning(), newRev->getSize(), newRev->getDiff(), origin->getN() );
                writeTable( table, *(rev->getOStream()) );

                delete cHandler;
                return newRev;
            }

    }
}
