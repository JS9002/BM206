#include "pml_hash.h"

//初始化哈希表（新建/覆盖） 

PMLHash::PMLHash(const char* file_path) {
	size_t mapped_len;
	int is_pmem; 
	if((start_addr = pmem_map_file(file_path, FILE_SIZE, PMEM_FILE_CREATE,
		0666, &mapped_len, &is_pmem)) == NULL){
		exit(0);
	}
	meta = (metadata*)start_addr;
	table = (pm_table*)((char*)start_addr + sizeof(meta));
    
        //初始化数据结构 
	if(meta->size == 0){
		meta->size = 1;
		meta->level = 0;
		meta->overflow_num = 0;
		meta->next = 1;

		for(int i = 0 ; i < MAX_IDX + 2 ; i++ ){
			table[i].fill_num = 0;
			table[i].next_offset = 0; 
		}
	}
}

//关闭文件 
PMLHash::~PMLHash() {
	pmem_unmap(start_addr, FILE_SIZE);
}

//分裂桶 
void PMLHash::split() {
	uint64_t index = meta->next;
	uint64_t newt = meta->next + (1<<(meta->level)) ;

	uint64_t i , j , k , tmp , tmp2;
	tmp = index;
	tmp2 = index;
	j = 0;
	k = 0;
	while(tmp2){        //由tmp2和i遍历要分裂的桶
		for(i = 0 ; i < table[tmp2].fill_num ; i++ ){
			//由tmp和j确定留在旧桶的数据的位置
			if(table[tmp2].kv_arr[i].key % (1<<(meta->level+1)) + 1 == index){
				table[tmp].kv_arr[j].key = table[tmp2].kv_arr[i].key;
				table[tmp].kv_arr[j].value = table[tmp2].kv_arr[i].value;
				if(++j == TABLE_SIZE){
					table[tmp].fill_num = TABLE_SIZE;
					tmp = table[tmp].next_offset;
					j = 0;
				}
			}
			else{   //由newt和k确定留在新桶的数据的位置
				if(k == TABLE_SIZE){
					table[newt].fill_num = TABLE_SIZE;
					newt = newOverflowIndex();
					k = 0;
				}
				table[newt].kv_arr[k].key = table[tmp2].kv_arr[i].key;
				table[newt].kv_arr[k].value = table[tmp2].kv_arr[i].value;
				k++;
			}
		}
        
		tmp2 = table[tmp2].next_offset;
	}
	
	recycle(table[tmp].next_offset); //回收溢出桶 

	table[tmp].fill_num = j;
	table[tmp].next_offset = 0;
	table[newt].fill_num = k;   //修改存有数据的桶的信息 
	meta->size++;
	if(meta->next == (1<<(meta->level))){
		meta->next = 1;
		meta->level++;   //原桶全部分裂，修改深度和next 
	}
	else meta->next++;
}

//计算原桶下标 
uint64_t PMLHash::hashFunc(const uint64_t &key ) {
	uint64_t index = key % (1<< meta->level) + 1;
	if(index < meta->next)
		index =  key % (1<< (meta->level+1)) + 1;
	return index; 
}


//分配一个溢出桶，返回其下标 
uint64_t PMLHash::newOverflowIndex(){
	uint64_t recycle_head = MAX_IDX + 1;   //回收桶的头部 
	uint64_t rh = recycle_head;
	uint64_t index;
	if(table[rh].next_offset){         //有可用的回收桶 
		index = table[rh].next_offset;
		table[rh].next_offset = table[index].next_offset;
	}
	else{
		index = MAX_IDX - (meta->overflow_num); //从右往左找一个空闲桶作溢出桶 
		if(index <= (meta->size)) index = 0;  //没有可用的溢出桶 
		if(index) meta->overflow_num++;
	}
	table[index].next_offset = 0;
	table[index].fill_num = 0;
	return index; 
}

void PMLHash::recycle(uint64_t index){
	uint64_t tmp = table[MAX_IDX + 1].next_offset;
	uint64_t tmp2 = index;
	table[index].fill_num = 0;
	while(table[tmp2].next_offset){         //回收一连串的溢出桶 
		tmp2 = table[tmp2].next_offset;
		table[tmp2].fill_num = 0;      //将回收桶的数据设为无效状态 
	}
	table[tmp2].next_offset = table[tmp].next_offset;
	table[MAX_IDX + 1].next_offset = index;
}

//插入键值对 
int PMLHash::insert(const uint64_t &key, const uint64_t &value) {
	uint64_t index = hashFunc(key);
	uint64_t tmp = index;
	
	while(table[tmp].fill_num == TABLE_SIZE){ //找到空的桶 
		if(table[tmp].next_offset == 0)
			table[tmp].next_offset = newOverflowIndex();
		tmp= table[tmp].next_offset;
		if(tmp == 0)      //没有可用溢出空间，插入失败 
			return -1;
	}
	
	table[tmp].kv_arr[table[tmp].fill_num].key = key;
	table[tmp].kv_arr[table[tmp].fill_num].value = value;
	table[tmp].fill_num++;

	if(tmp != index)
		split();
	pmem_persist(start_addr, FILE_SIZE);
	return 0;
}

//根据key查找相应的value 
int PMLHash::search(const uint64_t &key, uint64_t &value) {
	uint64_t tmp = hashFunc(key);
	while(tmp){  //遍历原桶及其所有溢出桶 
		for(int i = 0 ; i < table[tmp].fill_num ; i++ ){
			if(table[tmp].kv_arr[i].key == key){
				value = table[tmp].kv_arr[i].value;
				return 0;
			}
		}
		tmp = table[tmp].next_offset;
	}
	return -1;
}

//根据key删除某一键值对 
int PMLHash::remove(const uint64_t &key) {
	uint64_t index = hashFunc(key);
	uint64_t tmp = index;
	uint64_t tmp2 = index;

	while(table[tmp].next_offset){
		tmp2 = tmp;         //tmp2 index对应的“倒数第二”个桶（可能只有一个桶） 
		tmp = table[tmp].next_offset;   //tmp最后一个桶     
	}
	
	while(index){     
		for(int i = 0 ; i < table[index].fill_num ; i++ ){
			if(table[index].kv_arr[i].key == key){
				table[tmp].fill_num--;   //删除的位置需要由“最后一个溢出桶”的最后一个元素来填充 
				table[index].kv_arr[i].value = table[tmp].kv_arr[table[tmp].fill_num].value;
				table[index].kv_arr[i].key = table[tmp].kv_arr[table[tmp].fill_num].key;       

				if((table[tmp].fill_num == 0) && (tmp != index)){
					table[tmp2].next_offset = 0;
					recycle(tmp);    //回收空的溢出桶
				}
				pmem_persist(start_addr, FILE_SIZE);
				return 0;
			}
		}
		index = table[index].next_offset;
	}
	pmem_persist(start_addr, FILE_SIZE);
	return -1;
}

//更新某个key对应的value 
int PMLHash::update(const uint64_t &key, const uint64_t &value) {
	uint64_t tmp = hashFunc(key);  
	while(tmp){   //遍历查找 
		for(int i = 0 ; i < table[tmp].fill_num ; i++){
			if(table[tmp].kv_arr[i].key == key){
				table[tmp].kv_arr[i].value = value;
				pmem_persist(start_addr, FILE_SIZE);
				return 0;
			}
		}
		tmp = table[tmp].next_offset;
	}
	pmem_persist(start_addr, FILE_SIZE);
	return -1; 
}

//输出所有桶 
void PMLHash::print(){
	uint64_t tmp;
	int i , j;
	for(i = 1 ; i <= meta->size ; i++){
		tmp = i;
		j = 0;
		while(tmp){
			if(j == 0) cout << " table " << i << " :  " ;
			else cout << " ----overflow : " ;
			for(int k = 0 ; k < table[tmp].fill_num ; k++){
				cout << "( " << table[tmp].kv_arr[k].key << " , "<<table[tmp].kv_arr[k].value << ") ";  
			}
			cout << endl;
			tmp = table[tmp].next_offset;
			j++;
		}
		cout << endl << endl;
	}
}

void PMLHash::destroy(){  //数据清空 
	meta->size = 1;
	meta->level = 0;
	meta->overflow_num = 0;
	meta->next = 1;

	for(int i = 0 ; i < MAX_IDX + 2 ; i++ ){
		table[i].fill_num = 0;
		table[i].next_offset = 0; 
	}

	pmem_persist(start_addr, FILE_SIZE);
}
