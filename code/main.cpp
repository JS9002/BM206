#include "pml_hash.h"
 
int main() {
	PMLHash hash("/home/zwz/test/newfile");
	for (uint64_t i = 1; i <= 17; i++) {
	    hash.insert(i, i); 
	}
	cout << "Insert(1 ~ 17) OK!" << endl;
	hash.print();
	
	for (uint64_t i = 18; i <= 33; i++) {
	    hash.insert(i, i); 
	}
	cout << "Insert(18 ~ 33) OK!" << endl;
	hash.print();
	
	for (uint64_t i = 34; i <= 35; i++) {
	    hash.insert(i, i); 
	}
	cout << "Insert(34 ~ 35) OK!" << endl;
	hash.print();

	for (uint64_t i = 15; i <= 20; i++) {
		uint64_t val;
		hash.search(i, val);
		cout << "Search(key: " << i << ")--> (value: " << val << ")" << endl;
	}
	
	for (uint64_t i = 15; i <= 20; i++) {
	    hash.remove(i);
	}
	cout << "Remove(15 ~ 20) OK!" << endl;
	hash.print();
	
	for (uint64_t i = 25; i <= 30; i++) {
	    hash.update(i, i+1);
	    cout << "Update(" << i << ", " << i <<") -->" 
		<< "(" << i << ", " << i+1 << ") OK!" << endl; 
	}
	hash.print();
	return 0;
}
