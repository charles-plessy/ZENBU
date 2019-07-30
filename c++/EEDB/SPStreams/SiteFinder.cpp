/* $Id: SiteFinder.cpp,v 1.3 2018/03/16 07:11:34 severin Exp $ */

/***

NAME - EEDB::SPStreams::SiteFinder

SYNOPSIS

DESCRIPTION

A feature emitter which scans the genomic sequence for motif site patterns. 
Algorithm is based on PWM matric and the MATCH scoring method, but can be configured to search for 
string literals, IUPAC strings or PWM motifs.
General purpose site searching tool which can be configured for everything from:
 restriction sites
 Cas9 sites
 Transcription-factor binding sites

CONTACT

Jessica Severin <severin@gsc.riken.jp>

LICENSE

 * Software License Agreement (BSD License)
 * ZENBU system
 * copyright (c) 2007-2017 Jessica Severin RIKEN
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Jessica Severin RIKEN OSC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

APPENDIX

The rest of the documentation details each of the object methods. Internal methods are usually preceded with a _

***/


#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <rapidxml.hpp>  //rapidxml must be include before boost
#include <boost/algorithm/string.hpp>
#include <EEDB/Experiment.h>
#include <EEDB/Symbol.h>
#include <EEDB/SPStream.h>
#include <EEDB/SPStreams/SiteFinder.h>
#include <EEDB/WebServices/WebBase.h>

using namespace std;
using namespace MQDB;

const char*  EEDB::SPStreams::SiteFinder::class_name = "EEDB::SPStreams::SiteFinder";

//call out functions
//function prototypes
MQDB::DBObject*  _spstream_sitefinder_next_in_stream_func(EEDB::SPStream* node) {
  return ((EEDB::SPStreams::SiteFinder*)node)->_next_in_stream();
}
bool _spstream_sitefinder_stream_by_named_region_func(EEDB::SPStream* node, string assembly_name, string chrom_name, long int start, long int end) {
  return ((EEDB::SPStreams::SiteFinder*)node)->_stream_by_named_region(assembly_name, chrom_name, start, end);
}
void _spstream_sitefinder_reset_stream_node_func(EEDB::SPStream* node) {
  ((EEDB::SPStreams::SiteFinder*)node)->_reset_stream_node();
}
void _spstream_sitefinder_stream_clear_func(EEDB::SPStream* node) {
  ((EEDB::SPStreams::SiteFinder*)node)->_stream_clear();
}
void _spstream_sitefinder_delete_func(MQDB::DBObject *obj) { 
  delete (EEDB::SPStreams::SiteFinder*)obj;
}
string _spstream_sitefinder_display_desc_func(MQDB::DBObject *obj) { 
  return ((EEDB::SPStreams::SiteFinder*)obj)->_display_desc();
}
void _spstream_sitefinder_xml_func(MQDB::DBObject *obj, string &xml_buffer) { 
  ((EEDB::SPStreams::SiteFinder*)obj)->_xml(xml_buffer);
}


EEDB::SPStreams::SiteFinder::SiteFinder() {
  init();
}

EEDB::SPStreams::SiteFinder::~SiteFinder() {
  if(_region_chrom != NULL) { _region_chrom->release(); }
  if(_feature_source != NULL) { _feature_source->release(); }
  _region_chrom   = NULL;
  _feature_source = NULL;
}

void EEDB::SPStreams::SiteFinder::init() {
  EEDB::SPStream::init();
  _classname      = EEDB::SPStreams::SiteFinder::class_name;
  _module_name    = "SiteFinder";

  _source_stream   = NULL;
  _search_strand   = '='; //both
  _score_cutoff    = 1.0; //exact match
  _mismatches      = 0;  //exact match
  
  _current_strand = '+';
  _current_start  = -1;
  _current_count  = 1;
  _region_start   = -1;
  _region_end     = -1;
  _region_chrom   = NULL;
  
  _sequence_start = -1;
  
  _feature_source = new EEDB::FeatureSource();
  _feature_source->name("site_search_emitter");
  _feature_source->category("dynamic");
 
  //function pointer code
  _funcptr_delete                   = _spstream_sitefinder_delete_func;
  _funcptr_display_desc             = _spstream_sitefinder_display_desc_func;
  _funcptr_xml                      = _spstream_sitefinder_xml_func;
  _funcptr_simple_xml               = _spstream_sitefinder_xml_func;
 
  _funcptr_next_in_stream           = _spstream_sitefinder_next_in_stream_func;
  _funcptr_reset_stream_node        = _spstream_sitefinder_reset_stream_node_func;
  _funcptr_stream_clear             = _spstream_sitefinder_stream_clear_func;
  _funcptr_stream_by_named_region   = _spstream_sitefinder_stream_by_named_region_func;
}

string EEDB::SPStreams::SiteFinder::_display_desc() {
  return "SiteFinder";
}

string EEDB::SPStreams::SiteFinder::display_contents() {
  return display_desc();
}


////////////////////////////////////////////////////////////////////////////
// Instance methods
////////////////////////////////////////////////////////////////////////////

void  EEDB::SPStreams::SiteFinder::output_strand(string value) {
  if((value == "=") or (value == "both") or (value==" "))      { _search_strand = '='; }
  if((value == "pos") or (value=="+") or (value=="sense"))     { _search_strand = '+'; }
  if((value == "neg") or (value=="-") or (value=="antisense")) { _search_strand = '-'; }
}

void  EEDB::SPStreams::SiteFinder::iupac_sequence(string value) {
  fprintf(stderr, "set iupac [%s]\n", value.c_str());
  _pw_matrix.clear();
  _iupac_seq = value;
  for(unsigned i=0; i<value.length(); i++) {
    add_iupac_to_pwm(value[i]);
  }
}

void  EEDB::SPStreams::SiteFinder::clear_pw_matrix() {
  _pw_matrix.clear();
}

void  EEDB::SPStreams::SiteFinder::add_iupac_to_pwm(char value) {
  value = toupper(value);
  
  struct pwm_e pwm_row;
  
  switch(value) {
    case 'A':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 0.0, 0.0, 0.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 0.0;
      pwm_row.m[2] = 0.0;
      pwm_row.m[3] = 0.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'C':
      //memcpy(pwm_row.m, (double[5]) { 0.0, 1.0, 0.0, 0.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 0.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 0.0;
      pwm_row.m[3] = 0.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'G':
      //memcpy(pwm_row.m, (double[5]) { 0.0, 0.0, 1.0, 0.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 0.0;
      pwm_row.m[1] = 0.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 0.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'T':
      //memcpy(pwm_row.m, (double[5]) { 0.0, 0.0, 0.0, 1.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 0.0;
      pwm_row.m[1] = 0.0;
      pwm_row.m[2] = 0.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'N':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 1.0, 1.0, 1.0, 1.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 1.0;
      break;
    case 'R':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 0.0, 1.0, 0.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 0.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 0.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'Y':
      //memcpy(pwm_row.m, (double[5]) { 0.0, 1.0, 0.0, 1.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 0.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 0.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'K':
      //memcpy(pwm_row.m, (double[5]) { 0.0, 0.0, 1.0, 1.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 0.0;
      pwm_row.m[1] = 0.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'M':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 1.0, 0.0, 0.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 0.0;
      pwm_row.m[3] = 0.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'S':
      //memcpy(pwm_row.m, (double[5]) { 0.0, 1.0, 1.0, 0.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 0.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 0.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'W':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 0.0, 0.0, 1.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 0.0;
      pwm_row.m[2] = 0.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'B':
      //memcpy(pwm_row.m, (double[5]) { 0.0, 1.0, 1.0, 1.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 0.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'D':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 0.0, 1.0, 1.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 0.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'H':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 1.0, 0.0, 1.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 0.0;
      pwm_row.m[3] = 1.0;
      pwm_row.m[4] = 0.0;
      break;
    case 'V':
      //memcpy(pwm_row.m, (double[5]) { 1.0, 1.0, 1.0, 0.0, 0.0 }, sizeof(pwm_row));
      pwm_row.m[0] = 1.0;
      pwm_row.m[1] = 1.0;
      pwm_row.m[2] = 1.0;
      pwm_row.m[3] = 0.0;
      pwm_row.m[4] = 0.0;
      break;
    default: break;
  }
  
  _pw_matrix.push_back(pwm_row);
  
  /*
   static struct iupac_ids _trans_table[] = {
   // simple
   { 'A', { 1.0, 0.0, 0.0, 0.0, 0.0 } },
   { 'C', { 0.0, 1.0, 0.0, 0.0, 0.0 } },
   { 'G', { 0.0, 0.0, 1.0, 0.0, 0.0 } },
   { 'T', { 0.0, 0.0, 0.0, 1.0, 0.0 } },
   { 'N', { 1.0, 1.0, 1.0, 1.0, 1.0 } },
   
   { 'R', { 1.0, 0.0, 1.0, 0.0, 0.0 } },
   { 'Y', { 0.0, 1.0, 0.0, 1.0, 0.0 } },
   { 'K', { 0.0, 0.0, 1.0, 1.0, 0.0 } },
   { 'M', { 1.0, 1.0, 0.0, 0.0, 0.0 } },
   { 'S', { 0.0, 1.0, 1.0, 0.0, 0.0 } },
   { 'W', { 1.0, 0.0, 0.0, 1.0, 0.0 } },
   { 'B', { 0.0, 1.0, 1.0, 1.0, 0.0 } },
   { 'D', { 1.0, 0.0, 1.0, 1.0, 0.0 } },
   { 'H', { 1.0, 1.0, 0.0, 1.0, 0.0 } },
   { 'V', { 1.0, 1.0, 1.0, 0.0, 0.0 } },
   { 0, { 0.0 } }
   };
   */

}

////////////////////////////////////////////////////////////////////////////
//
// override method for subclasses which will
// do all the work
//
///////////////////////////////////////////////////////////////////////////


void  EEDB::SPStreams::SiteFinder::_reset_stream_node() {
  _current_start  = -1;
  _current_count  = 1;
  _region_start   = -1;
  _region_end     = -1;
  _current_strand = '+';
  _sequence_start = -1;
  _sequence.clear();
  if(_region_chrom != NULL) { _region_chrom->release(); }
  _region_chrom   = NULL;
}


void EEDB::SPStreams::SiteFinder::_stream_clear() {
  _current_start  = -1;
  _current_count  = 1;
  _region_start   = -1;
  _region_end     = -1;
  _current_strand = '+';
  _sequence_start = -1;
  _sequence.clear();
  if(_region_chrom != NULL) { _region_chrom->release(); }
  _region_chrom   = NULL;
}


bool EEDB::SPStreams::SiteFinder::_stream_by_named_region(
        string assembly_name, string chrom_name, long int start, long int end) {
  EEDB::Assembly *asmb = EEDB::Assembly::cache_get_assembly(assembly_name);
  if(!asmb) {
    EEDB::WebServices::WebBase::global_parameters["error_message"] += "unable to get assembly[" + assembly_name + "]\n";
    return false;
  }
  
  _region_chrom = asmb->get_chrom(chrom_name);
  if(!_region_chrom) {
    EEDB::WebServices::WebBase::global_parameters["error_message"] += "unable to get assembly[" + assembly_name + "]  chrom["+chrom_name+"]\n";
    return false;
  }
  
  if(start < 0) { start = 1; }
  
  _current_strand = '+';
  if(_search_strand == '-') {
    _current_strand = '-';
  }
  
  _current_start = start;
  _region_start  = start;
  _region_end    = end;
  
  return true;
}


MQDB::DBObject* EEDB::SPStreams::SiteFinder::_next_in_stream() {
  if(_current_start < 0)    { return NULL; }  //finished
  if(_region_chrom == NULL) { return NULL; }  //finished
  
  /*
  if((_sequence_start <0) ||
     ((_current_start + _pw_matrix.size() + 1) >= (_sequence_start + _sequence.length() - 1))) {
    unsigned long seq_len = 10000;
    if(_region_end>0) { seq_len = _region_end - _current_start + _pw_matrix.size() +1; }
    if(seq_len > 10000) { seq_len = 10000; }
    if(seq_len < _pw_matrix.size() + 1) { seq_len = _pw_matrix.size() + 1; }
    
    _sequence_start = _current_start;
    _sequence = _region_chrom->get_subsequence(_sequence_start, _sequence_start + seq_len, "+");
    fprintf(stderr, "get genome seq %s  %ld for %ld bp\n", _region_chrom->chrom_name().c_str(), _sequence_start, seq_len);
    fprintf(stderr, "%s\n", _sequence.c_str());
  }
  */
  
  EEDB::Feature *feature = NULL;
  //while(_current_start < _sequence_start + _sequence.length() - _pw_matrix.size()) {
  while(_current_start >= 0) {
    
    if(_region_end>0 && (_current_start > _region_end)) { //finished
      _current_start = -1;
      _sequence_start = -1;
      _sequence.clear();
      return NULL;
    }

    if(!_load_next_sequence()) {
      _current_start = -1;
      _sequence_start = -1;
      _sequence.clear();
      return NULL;
    }
    
    string tag = _sequence.substr(_current_start - _sequence_start, _pw_matrix.size());
    
    if(_current_strand=='-') {
      //do rev-complement
      std::reverse(tag.begin(),tag.end());
      string::iterator it1;
      string rosette1 ="acgtrymkswhbvdnxACGTRYMKSWHBVDNX";
      string rosette2 ="tgcayrkmswdvbhnxTGCAYRKMSWDVBHNX";
      for(it1=tag.begin(); it1!=tag.end(); it1++) {
        size_t found = rosette1.find(*it1);
        if (found!=std::string::npos) {
          *it1 = rosette2[found];
        }
      }
    }
  
    if(_check_sequence(tag)) {
      feature = EEDB::Feature::realloc();
      feature->feature_source(_feature_source);
      feature->chrom(_region_chrom);
      feature->chrom_start(_current_start);
      feature->chrom_end(_current_start + _pw_matrix.size() - 1);
      feature->strand(_current_strand);
      feature->metadataset()->add_tag_data("site_sequence", tag);
    }
    
    if(_search_strand == '=') {
      if(_current_strand=='+') {
        _current_strand = '-';
      } else {
        _current_strand = '+';
        _current_start++;
      }
    } else {
      _current_start++;
    }

    if(feature) { return feature; }
  }
  
  _current_start = -1;
  _sequence_start = -1;
  _sequence.clear();
  return NULL;
}


bool  EEDB::SPStreams::SiteFinder::_load_next_sequence() {
  if(_current_start < 0) { return false; }
  
  if((_sequence_start >0) &&
     (_current_start <= _sequence_start + (long)_sequence.length() - (long)_pw_matrix.size())) {
    return true;
  }
  
  if(_current_start + (long)_pw_matrix.size() -1 > _region_chrom->chrom_length()) { return false; }
  
  //if((_sequence_start >0) &&
  //   ((_current_start + _pw_matrix.size() + 1) < (_sequence_start + _sequence.length() - 1))) {
  //  return;
  //}
  
  unsigned long seq_len = 200000;
  if(_region_end>0) { seq_len = _region_end - _current_start + _pw_matrix.size() +1; }
  if(seq_len > 200000) { seq_len = 200000; }
  if(seq_len < _pw_matrix.size() + 1) { seq_len = _pw_matrix.size() + 1; }
  
  _sequence_start = _current_start;
  _sequence = _region_chrom->get_subsequence(_sequence_start, _sequence_start + seq_len, "+");
  
  if(_sequence.empty()) { return false; }
  
  fprintf(stderr, "get genome seq %s  %ld for %ld bp, ret %ld bp\n",
          _region_chrom->chrom_name().c_str(), _sequence_start, seq_len,
          _sequence.length());
  //fprintf(stderr, "%s\n", _sequence.c_str());
  return true;
}


bool  EEDB::SPStreams::SiteFinder::_check_sequence(string seq) {
  //perfect match test
  if(_pw_score_sequence(seq) < _pw_matrix.size()) { return false; }
  return true;
}


double  EEDB::SPStreams::SiteFinder::_pw_score_sequence(string seq) {
  if(seq.length() < _pw_matrix.size()) { return 0; }
  
  //fprintf(stderr, "_pw_score_sequence [%s]\n", seq.substr(0,_pw_matrix.size()).c_str());
  double score=0.0;
  for(unsigned i=0; i<_pw_matrix.size(); i++) {
    char bp = toupper(seq[i]);
    
    double val1=0.0;
    switch(bp) {
      case 'A':
        val1 = _pw_matrix[i].m[0];
        break;
      case 'C':
        val1 = _pw_matrix[i].m[1];
        break;
      case 'G':
        val1 = _pw_matrix[i].m[2];
        break;
      case 'T':
        val1 = _pw_matrix[i].m[3];
        break;
      case 'N':
        val1 = _pw_matrix[i].m[4];
        break;
      default:
        break;
    }
    score += val1;
    if((val1 < 0.5) && (_mismatches == 0)) { return score; }
    //fprintf(stderr, "%d [%c] %1.3f  %1.3f\n", i, bp, val1, score);
  }
  return score;
}


/******************************************************************************/


void EEDB::SPStreams::SiteFinder::_xml(string &xml_buffer) {
  char buffer[256];
  _xml_start(xml_buffer);  //from superclass
  
  snprintf(buffer, 256, "<strand>%c</strand>", _search_strand);
  xml_buffer.append(buffer);

  if(_mismatches >=0) {
    snprintf(buffer, 256, "<mismatches>%ld</mismatches>", _mismatches);
    xml_buffer.append(buffer);
  }
  
  if(!_iupac_seq.empty()) {
    xml_buffer += "<iupac_seq>" + _iupac_seq +"</iupac_seq>";
  }

  _xml_end(xml_buffer);  //from superclass
}


EEDB::SPStreams::SiteFinder::SiteFinder(void *xml_node) {
  //constructor using a rapidxml <spstream> description
  init();
  if(xml_node==NULL) { return; }

  rapidxml::xml_node<>      *root_node = (rapidxml::xml_node<>*)xml_node;
  rapidxml::xml_node<>      *node;
  
  if(string(root_node->name()) != "spstream") { return; }
  
  if((node = root_node->first_node("strand")) != NULL) {
    output_strand(node->value());
  }

  if((node = root_node->first_node("mismatches")) != NULL) {
    _mismatches = strtol(node->value(), NULL, 10);
  }

  if((node = root_node->first_node("iupac_seq")) != NULL) {
    iupac_sequence(node->value());
  }
}

