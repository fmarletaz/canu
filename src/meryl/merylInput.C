
/******************************************************************************
 *
 *  This file is part of 'sequence' and/or 'meryl', software programs for
 *  working with DNA sequence files and k-mers contained in them.
 *
 *  Modifications by:
 *
 *    Brian P. Walenz beginning on 2018-FEB-26
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *  File 'README.license' in the root directory of this distribution contains
 *  full conditions and disclaimers.
 */

#include "meryl.H"


#undef DEBUG_INPUT


merylInput::merylInput(merylOperation *o) {
  _operation   = o;
  _stream      = NULL;
  _sequence    = NULL;
  _store       = NULL;

  _count       = 0;
  _valid       = false;

  _sqBgn       = 0;
  _sqEnd       = 0;

  _read        = NULL;
  _readData    = NULL;
  _readID      = 0;
  _readPos     = UINT32_MAX;

  memset(_name, 0, FILENAME_MAX+1);
  strncpy(_name, toString(_operation->getOperation()), FILENAME_MAX);
}



merylInput::merylInput(const char *n, kmerCountFileReader *s) {
  _operation   = NULL;
  _stream      = s;
  _sequence    = NULL;
  _store       = NULL;

  _count       = 0;
  _valid       = false;

  _sqBgn       = 0;
  _sqEnd       = 0;

  _read        = NULL;
  _readData    = NULL;
  _readID      = 0;
  _readPos     = UINT32_MAX;

  memset(_name, 0, FILENAME_MAX+1);
  strncpy(_name, n, FILENAME_MAX);
}



merylInput::merylInput(const char *n, dnaSeqFile *f) {
  _operation   = NULL;
  _stream      = NULL;
  _sequence    = f;
  _store       = NULL;

  _count       = 0;
  _valid       = true;    //  Trick nextMer into doing something without a valid mer.

  _sqBgn       = 0;
  _sqEnd       = 0;

  _read        = NULL;
  _readData    = NULL;
  _readID      = 0;
  _readPos     = UINT32_MAX;

  memset(_name, 0, FILENAME_MAX+1);
  strncpy(_name, n, FILENAME_MAX);
}



merylInput::merylInput(const char *n, sqStore *s, uint32 segment, uint32 segmentMax) {
  _operation   = NULL;
  _stream      = NULL;
  _sequence    = NULL;
  _store       = s;

  _count       = 0;
  _valid       = true;    //  Trick nextMer into doing something without a valid mer.

  _sqBgn       = 1;                                   //  C-style, not the usual
  _sqEnd       = _store->sqStore_getNumReads() + 1;   //  sqStore semantics!

  if (segmentMax > 1) {
    uint64  nBases = 0;

    for (uint32 ss=1; ss <= _store->sqStore_getNumReads(); ss++)
      nBases += _store->sqStore_getRead(ss)->sqRead_sequenceLength();

    uint64  nBasesPerSeg = nBases / segmentMax;

    _sqBgn = 0;
    _sqEnd = 0;

    nBases = 0;

    for (uint32 ss=1; ss <= _store->sqStore_getNumReads(); ss++) {
      nBases += _store->sqStore_getRead(ss)->sqRead_sequenceLength();

      if ((_sqBgn == 0) && ((nBases / nBasesPerSeg) == segment - 1))
        _sqBgn = ss;

      if ((_sqEnd == 0) && ((nBases / nBasesPerSeg) == segment))
        _sqEnd = ss;
    }

    if (segment == segmentMax)                      //  Annoying special case; if the last segment,
      _sqEnd = _store->sqStore_getNumReads() + 1;   //  sqEnd is set to the last read, not N+1.

    fprintf(stderr, "merylInput-- segment %u/%u picked reads %u-%u out of %u\n",
            segment, segmentMax, _sqBgn, _sqEnd, _store->sqStore_getNumReads());
  }

  _read        = NULL;
  _readData    = new sqReadData;
  _readID      = _sqBgn - 1;       //  Incremented before loading the first read
  _readPos     = 0;

  memset(_name, 0, FILENAME_MAX+1);
  strncpy(_name, n, FILENAME_MAX);
}



merylInput::~merylInput() {
#ifdef DEBUG_INPUT
  fprintf(stderr, "Destroy input %s\n", _name);
#endif

  delete _stream;
  delete _operation;
  delete _sequence;

  _store->sqStore_close();
}



void
merylInput::initialize(void) {
  if (_operation)
    _operation->initialize();
}



void
merylInput::nextMer(void) {
  char kmerString[256];

  if (_stream) {
#ifdef DEBUG_INPUT
    fprintf(stderr, "merylIn::nextMer('%s')--\n", _name);
#endif

    _valid = _stream->nextMer();
    _kmer  = _stream->theFMer();
    _count = _stream->theCount();

#ifdef DEBUG_INPUT
    fprintf(stderr, "merylIn::nextMer('%s')-- now have valid=" F_U32 " kmer %s count " F_U64 "\n",
            _name, _valid, _kmer.toString(kmerString), _count);
    fprintf(stderr, "\n");
#endif
  }

  if (_operation) {
#ifdef DEBUG_INPUT
    fprintf(stderr, "merylIn::nextMer(%s)--\n", _name);
#endif

    _valid = _operation->nextMer();
    _kmer  = _operation->theFMer();
    _count = _operation->theCount();

#ifdef DEBUG_INPUT
    fprintf(stderr, "merylIn::nextMer(%s)-- now have valid=" F_U32 " kmer %s count " F_U64 "\n",
            _name, _valid, _kmer.toString(kmerString), _count);
    fprintf(stderr, "\n");
#endif
  }

#ifdef DEBUG_INPUT
  if (_sequence)
    fprintf(stderr, "merylIn::nextMer(%s)--\n", _name);
#endif

#ifdef DEBUG_INPUT
  if (_store)
    fprintf(stderr, "merylIn::nextMer(%s)--\n", _name);
#endif
}



bool
merylInput::loadBases(char    *seq,
                      uint64   maxLength,
                      uint64  &seqLength,
                      bool    &endOfSequence) {

  if (_stream) {
    return(false);
  }

  if (_operation) {
    return(false);
  }

  if (_sequence) {
    return(_sequence->loadBases(seq, maxLength, seqLength, endOfSequence));
  }

  if (_store) {

    //  If no read currently loaded, load one, or return that we're done.

    if ((_read    == NULL) ||
        (_readPos >= _read->sqRead_sequenceLength())) {
      _readID++;

      if (_readID >= _sqEnd)  //  C-style iteration, not usual sqStore semantics.
        return(false);

      _read    = _store->sqStore_getRead(_readID);
      _readPos = 0;

      _store->sqStore_loadReadData(_read, _readData);
    }

    //  How much of the read is left to return?

    uint32  len = _read->sqRead_sequenceLength() - _readPos;

    assert(len > 0);

    //  If the output space is big enough to hold the rest of the read, copy it,
    //  flagging it as the end of a sequence, and setup to load the next read.

    if (len < maxLength) {
      memcpy(seq, _readData->sqReadData_getSequence() + _readPos, sizeof(char) * len);

      _read          = NULL;

      seqLength      = len;
      endOfSequence  = true;
    }

    //  Otherwise, only part of the data will fit in the output space.

    else {
      memcpy(seq, _readData->sqReadData_getSequence() + _readPos, sizeof(char) * maxLength);

      _readPos      += maxLength;

      seqLength      = maxLength;
      endOfSequence  = false;
    }

    return(true);
  }

  return(false);
}
