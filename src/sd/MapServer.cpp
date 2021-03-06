#include "MapServer.h"


void TCPMapServer::wcallback(Handler* wcallback, msg_t type){
    TCPServer::wcallback(wcallback, type);
}

void TCPMapServer::rcallback(Handler* handler, msg_t type){     
    char* data = handler->get_in_data();
    
    if( type == EXISTS_OBJECT){
        char *buff = new char[DIGEST_LENGTH + 1 + HEADER_LENGTH];
        char* buffer = buff + HEADER_LENGTH;
        memcpy(buffer, data, DIGEST_LENGTH);
        
        m_objects->lock();
        std::string stre = digest_to_string(data);
        buffer[DIGEST_LENGTH] = ( objects->find( digest_to_string(data) ) != objects->end() );
        m_objects->unlock();
        
        handler->clear();
        
        handler->send(OBJECT, buff, DIGEST_LENGTH+1); //transfert ownership
        register_event(handler, EPOLLOUT, EPOLL_CTL_MOD);
    }else if( type == EXISTS_CHUNKS ){
        char* end = data + sizeof(uint64_t);
        uint64_t num = strtoull(data, &end, 0);
        
        char *buf = new char[ DIGEST_LENGTH * num + num + HEADER_LENGTH + uint64_s ];
        char *buffer = buf + HEADER_LENGTH;
        sprintf(buffer, "%" PRIu64 "", num);
        buffer+=uint64_s;
        
        uint64_t pos = 0; 
        data += sizeof(uint64_t);
        
        m_chunks->lock();
        for(int i = 0 ; i<num; i++, data+=DIGEST_LENGTH, 
        pos += DIGEST_LENGTH + 1){
            memcpy(buffer + pos, data, DIGEST_LENGTH);
            buffer[pos + DIGEST_LENGTH] = chunks->exists_digest( data );
        }
        m_chunks->unlock();

        handler->send(CHUNKS, buf, DIGEST_LENGTH * num + num + HEADER_LENGTH);//transfert ownership
        register_event(handler, EPOLLOUT, EPOLL_CTL_MOD);
    }else if( type == EXISTS_BIN ){
        char* end = data + sizeof(uint64_t) + DIGEST_LENGTH;
        uint64_t num = strtoull(data + DIGEST_LENGTH, &end, 0);
        uint64_t number = 0;// chunks to send back
        
        m_bins->lock();
        BinBlock* bin = bins->get_bin(data);
        if( bin != NULL ){
            char* tmp = data + DIGEST_LENGTH + uint64_s;
            for(int i = 0 ; i<num; ++i, tmp+=DIGEST_LENGTH){
                if( !bin->exists( tmp ) )
                    ++number;
            }
            
        }
        
        char* buf(NULL);
        size_t buf_len(0);
        
        if( number != num && bin !=NULL ){
            buf_len = DIGEST_LENGTH * number + HEADER_LENGTH + uint64_s + DIGEST_LENGTH;
            buf = new char[ buf_len ];
            
            char *buffer = buf + HEADER_LENGTH;
            memcpy( buffer, data, DIGEST_LENGTH);//id of bin
            sprintf(buffer + DIGEST_LENGTH, "%" PRIu64 "", number);
            buffer+=uint64_s + DIGEST_LENGTH;
            
            uint64_t pos = 0; 
            data += sizeof(uint64_t) + DIGEST_LENGTH;
            
            for(int i = 0 ; i<num; i++, data+=DIGEST_LENGTH, 
            buffer += DIGEST_LENGTH){
                if( !bin->exists( data ) )//on envoit que ce qui doit être sauvegardé
                    memcpy(buffer, data, DIGEST_LENGTH);
            }
            
            handler->send(BIN, buf, buf_len);//transfert ownership
        }else{ //all must be stored, format id_bin|num
            buf_len = DIGEST_LENGTH + HEADER_LENGTH ;
            buf = new char[ buf_len ];
            
            char *buffer = buf + HEADER_LENGTH;
            memcpy( buffer, data, DIGEST_LENGTH);//id of bin

            handler->send(WHOLE_BIN, buf, buf_len);//transfert ownership
        }
        m_bins->unlock();
        
        register_event(handler, EPOLLOUT, EPOLL_CTL_MOD);
    
    }else if( type == ADD_OBJECT ){
        char *buff = new char[DIGEST_LENGTH + HEADER_LENGTH];
        char *buffer = buff + HEADER_LENGTH;
        memcpy(buffer, data, DIGEST_LENGTH);
        
        m_objects->lock();
        (*objects)[ digest_to_string(buffer) ]=true;
        m_objects->unlock();
        
        handler->clear();

        handler->send(OBJECT_ADDED, buff, DIGEST_LENGTH + HEADER_LENGTH); //transfert ownership
        register_event(handler, EPOLLOUT, EPOLL_CTL_MOD);
    }else if( type == ADD_CHUNKS ){
        char* end = data + uint64_s;
        uint64_t num = strtoull(data, &end, 0);

        char *buff = new char[ DIGEST_LENGTH * num + HEADER_LENGTH + uint64_s];
        memcpy(buff + HEADER_LENGTH, data, DIGEST_LENGTH * num + uint64_s);

        data += uint64_s;
        m_chunks->lock();
        for(int i = 0 ; i<num; i++, data+=DIGEST_LENGTH)      
            chunks->add_digest( data );
        
        m_chunks->unlock();

        handler->send(CHUNKS_ADDED, buff, DIGEST_LENGTH * num + HEADER_LENGTH + uint64_s);//transfert ownership
        register_event(handler, EPOLLOUT, EPOLL_CTL_MOD);
    }else if( type == ADD_BIN ){
        char* end = data + sizeof(uint64_t) + DIGEST_LENGTH;
        uint64_t num = strtoull(data + DIGEST_LENGTH, &end, 0);

        BinBlock bin("", data + DIGEST_LENGTH + uint64_s, num);
        bin.set_id( data );
        
        m_bins->lock();
        bins->add_bin(&bin);
        m_bins->unlock();
        
        char* buf=new char[ DIGEST_LENGTH + HEADER_LENGTH ];
        memcpy(buf+HEADER_LENGTH, data, DIGEST_LENGTH); //id of bin
        
        handler->clear();
        handler->send(BIN_ADDED, buf, DIGEST_LENGTH + HEADER_LENGTH); //transfert ownership
        register_event(handler, EPOLLOUT, EPOLL_CTL_MOD);
    }else{
        TCPServer::rcallback(handler, type);
    }
}

MapServer::MapServer(const char* port, NodeMap* _nodes, const char* path) : nodes(_nodes){
    chunks = new BTree( (path/fs::path("chunks")).string().c_str() );
    bins = new BinTree( (path/fs::path("bins")).string().c_str() );
    pipe(pfds);
    alive.store(true,std::memory_order_relaxed); 
    
    server = new TCPMapServer(port, pfds[0], &alive, &tasks, &m_tasks,
    &objects, &m_objects, chunks, &m_chunks, bins, &m_bins);
    
    t_server = std::thread( run_server,server );
    t_server.detach();
} 

MapServer::~MapServer(){
    delete chunks;
    delete bins;
}

void MapServer::run(){
    while( true ){
        sleep( 5 );
    }
}
