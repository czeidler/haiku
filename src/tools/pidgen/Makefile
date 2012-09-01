CC=g++
LEX=flex
YACC=bison -y
CFLAGS=-c -Wall
LDFLAGS=
SOURCES:= \
	AST.cpp \
	CPPBackend.cpp \
	CustomizableBackend.cpp \
	idlstruct.cpp \
	InterfaceRec.cpp \
	main.cpp \
	OutputCPP.cpp \
	OutputI.cpp \
	OutputUtil.cpp \
	PaladinBackend.cpp \
	symbol.cpp \
	symbolstack.cpp \
	TypeBank.cpp \
\
	support/SharedBuffer.cpp \
	support/SortedVector.cpp \
	support/String.cpp \
	support/TextStream.cpp \
	support/Vector.cpp \
	support/VectorIO.cpp \
	support/VFSFile.cpp

#	yacc.y
#	scanner.l

OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=pidgen

INCLUDES=-Isupport/ -Ilinux/


all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@