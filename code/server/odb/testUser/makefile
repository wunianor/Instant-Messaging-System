.PHONY:clean

all:test_client

test_client:test_client.cc user-odb.cxx
	g++ -o $@ $^ -lodb-mysql -lodb -lodb-boost

clean:
	rm -f test_client