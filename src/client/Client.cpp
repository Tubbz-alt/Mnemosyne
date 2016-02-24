#include "Client.h"

void TCPClientServer::wcallback(Handler* handler, msg_t type){
	close( handler->get_fd() );
	free( handler );
}

void TCPClientServer::rcallback(Handler* handler, msg_t type){	
	if( type ==  OBJECT){
		Msg m( handler->get_in_data());
		char digest[SHA224_DIGEST_LENGTH];
		char exists;
		memcpy(digest, m.get_data(), SHA224_DIGEST_LENGTH);
		memcpy(&exists, m.get_data()+SHA224_DIGEST_LENGTH, sizeof(char));
		
		m_objects->lock();
		objects->push_back( make_pair(digest, exists) );
		m_objects->unlock();
	}else if( type == OBJECTS ){
		Msg m( handler->get_in_data());
		size_t num = 0;
		char* end = m.get_data() + sizeof(size_t);
		num = strtoull(m.get_data(), &end, 0);
		
		int i = 0;
		char* ptr = m.get_data() + sizeof(size_t);
		for(; i<num; i++, ptr+=sizeof(char)+SHA224_DIGEST_LENGTH){
			char digest[SHA224_DIGEST_LENGTH];
			char exists;
			memcpy(digest, ptr, SHA224_DIGEST_LENGTH);
			memcpy(&exists, ptr+SHA224_DIGEST_LENGTH, 1);
		
			m_objects->lock();
			objects->push_back( make_pair(digest, exists) );
			m_objects->unlock();
		}
	}
	
	struct epoll_event listen_event;
	listen_event.events = EPOLLOUT | EPOLLET;	
	listen_event.data.ptr=static_cast<void*>( handler );
	printf("Msg incomming : |");
	write (1, handler->get_in_data(), handler->get_in_offset()); //Debug

	s = epoll_ctl (efd, EPOLL_CTL_MOD, handler->get_fd(), &listen_event);
	if(s == -1){
		perror("epoll_ctl-rcallback");
		close( handler->get_fd() );
		free( handler );
		abort();
	}
	
}


///Save functions
///agregate digest in : sizedigest1digest2....
void Client::build_digests(list<Chunk*>& chunks, char* digests){
	list<Chunk*>::iterator it = chunks.begin();
	
	sprintf(digests, "%lu", chunks.size());
	
	for(int k=sizeof(size_t); it != chunks.end() ; k += SHA224_DIGEST_LENGTH)
		memcpy(digests+k, (*it)->_digest(), SHA224_DIGEST_LENGTH);
}

void Client::buid_digests_map(vector<Chunk>& chunks, map<string, Chunk*>& map){
	vector<Chunk>::iterator it = chunks.begin();

	for(; it != chunks.end() ; it++)
		map[ string(it->_digest()) ] = &(*it);
}

void Client::group_by_id(vector<Chunk>& chunks, map<uint64_t, list<Chunk*> >& buffers){
	uint64_t tmp_id;
	for(int i=0; i<chunks.size(); i++){
		tmp_id = nodes->rallocate( chunks[i]._digest(), SHA224_DIGEST_LENGTH )->get_id();
		
		if( buffers.find(tmp_id) == buffers.end() )
			buffers[tmp_id]=list<Chunk*>();
		
		buffers[tmp_id].push_back( &chunks[i] );
	}
} 

void Client::group_by_ids(list<Chunk*>& chunks, map<uint64_t, list<Chunk*> >& buffers){
	list<Chunk*>::iterator it = chunks.begin();
	vector<Node*> _nodes;
	uint64_t tmp_id;
	
	for(; it != chunks.end() ; it++){
		_nodes = nodes->wallocate( (*it)->_digest(), SHA224_DIGEST_LENGTH );
		
		for( int k=0; k< _nodes.size(); k++){
			tmp_id = _nodes[k]->get_id();
			
			if( buffers.find(tmp_id) == buffers.end() )
				buffers[tmp_id]=list<Chunk*>();
			
			buffers[tmp_id].push_back( (*it) );
		}
	}
} 

///n act as a condition
bool Client::wait_objects(int n){
	struct timespec req, rem;
	uint64_t duration = 0;
	bool flag =false;
	
	req.tv_sec  = 0;
	req.tv_nsec = DELAY; //0.001 s
	
	while( flag && duration < TIME_OUT){
		nanosleep(&req, &rem);
		duration += DELAY;
		
		m_objects.lock();
		flag = (objects.size() == n);
		m_objects.unlock();
	}
	
	return flag;
}

///true : file already exists, else not
bool Client::dedup_by_file(const char* location, char* file_digest){
	if( !hashfile(location, file_digest) ){
		perror("Hashing failed");
		return false;
	}
	Node* prime;
	
	Msg m(EXISTS_OBJECT, file_digest, SHA224_DIGEST_LENGTH);
	prime = nodes->rallocate( file_digest, SHA224_DIGEST_LENGTH);
	send(m, prime->get_host(), prime->get_port());
	
	if( !wait_objects( 1 ) )
		return false;
	
	if( objects.front().second ){//file exists
		clear_objects();
		return true;
	}else
		clear_objects();
		
	return false;
}

bool Client::save(const char* name, const char* location, fs::path path_dir){
	char file_digest[SHA224_DIGEST_LENGTH];
	if( dedup_by_file(location, file_digest) )
		return true;
	
	vector<Chunk> chunks;
	map<string, Chunk*> chunks_map; 
	map<uint64_t, list<Chunk*> > buffers; //node_id => chunks of this node

	
	chunks = cf->split(location);
	buid_digests_map( chunks, chunks_map);
		
	///Send requests
	group_by_id( chunks, buffers);
	
	for(map<uint64_t, list<Chunk*> >::iterator it = buffers.begin() ; 
	it != buffers.end(); it++){
		size_t buffer_len = (it->second).size() * SHA224_DIGEST_LENGTH + sizeof(size_t);
		Msg* m = new Msg(EXISTS_OBJECTS, buffer_len); //handle by send
		
		build_digests( it->second, m->get_data());
		
		Node* node = nodes->get_node( it->first );
		send(m, node->get_host(), node->get_port());
	}
	
	if( !wait_objects( chunks.size() ) )
		return false;
	
	///Select chunks to dedup
	list<Chunk*> to_dedup;
	char* tmp_digest;
	char tmp_exists;
	
	m_objects.lock();
	for(int i=0; i<objects.size(); i++){
		tmp_digest = objects.front().first;
		tmp_exists = objects.front().second;
		if( !tmp_exists && chunks_map.find(tmp_digest) != chunks_map.end()){
			to_dedup.push_back( chunks_map[ tmp_digest ] );
		}
		objects.pop_front();
	}
	m_objects.unlock();
	
	///Store chunks
	if( to_dedup.size() > 0){				
		if( to_dedup.front()->get_data() != NULL ){
			for(list<Chunk*>::iterator it = to_dedup.begin() ; 
			it != to_dedup.end(); it++){	
				string tmp=(path_dir/fs::path((*it)->c_digest())).string();
				ofstream c_file(tmp.c_str(), ios::binary);
				c_file<< (*it)->get_data();
			}
		}
		else{	
			char *src;
			int fd = open(location, O_RDONLY);
			uint64_t size_file = lseek(fd, 0, SEEK_END);
			src = static_cast<char*>( 
				mmap(NULL, size_file, PROT_READ, MAP_PRIVATE, fd, 0));
			munmap( src, size_file);
			
			for(list<Chunk*>::iterator it = to_dedup.begin() ; 
			it != to_dedup.end(); it++){
				string tmp=(path_dir/fs::path((*it)->c_digest())).string();
				ofstream c_file(tmp.c_str(), ios::binary);
				
				c_file.write( src+(*it)->get_begin(), (*it)->get_length() );
				c_file.close();
			}
			 
			close( fd );	
		}
		
		///Send new chunk to sd
		map<uint64_t, list<Chunk*> > _buffers;
		group_by_ids( to_dedup, _buffers);
		
		for(map<uint64_t, list<Chunk*> >::iterator it = buffers.begin() ; 
		it != buffers.end(); it++){
			size_t buffer_len = (it->second).size() * SHA224_DIGEST_LENGTH + sizeof(size_t);
			Msg* m = new Msg(ADD_OBJECTS, buffer_len); //handle by send
			
			build_digests( it->second, m->get_data());
			
			Node* node = nodes->get_node( it->first );
			send(m, node->get_host(), node->get_port());
		}
		
		///Send file digest
		vector<Node*> _nodes = nodes->wallocate( file_digest, SHA224_DIGEST_LENGTH );
		for(int k=0 ; k< _nodes.size() ; k++){
			Msg* m = new Msg(ADD_OBJECT, SHA224_DIGEST_LENGTH); //handle by send
			for(int i=0; i <SHA224_DIGEST_LENGTH; i++)
				sprintf(m->get_data()+i, "%02x", file_digest[i]);
		
			send(m, _nodes[k]->get_host(), _nodes[k]->get_port());
		}
		
		
		//
		// I	l faut vérifier que tous les serveurs distants aient répondu
		//
	}
	clear_objects();
	
	
	
	if( !buildMetadata(name, chunks, path_dir) )
		return false;
	
	return true;
}
