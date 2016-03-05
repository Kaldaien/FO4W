/**
 * This file is part of Batman "Fix".
 *
 * Batman "Fix" is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * The Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Batman "Fix" is distributed in the hope that it will be useful,
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Batman "Fix". If not, see <http://www.gnu.org/licenses/>.
**/

#ifndef __BMF__COMMAND_H__
#define __BMF__COMMAND_H__

#ifdef _MSC_VER
# include <unordered_map>
//# include <hash_map>
//# define hash_map stdext::hash_map
#else
# include <hash_map.h>
#endif

#include <locale> // tolower (...)

template <typename T>
class BMF_VarStub;// : public BMF_Variable

class BMF_Variable;
class BMF_Command;

class BMF_CommandResult
{
public:
  BMF_CommandResult (       std::string   word,
                            std::string   arguments = "",
                            std::string   result    = "",
                            int           status = false,
                      const BMF_Variable* var    = NULL,
                      const BMF_Command*  cmd    = NULL ) : word_   (word),
                                                            args_   (arguments),
                                                            result_ (result) {
    var_    = var;
    cmd_    = cmd;
    status_ = status;
  }

  std::string         getWord     (void) const { return word_;   }
  std::string         getArgs     (void) const { return args_;   }
  std::string         getResult   (void) const { return result_; }

  const BMF_Variable* getVariable (void) const { return var_;    }
  const BMF_Command*  getCommand  (void) const { return cmd_;    }

  int                 getStatus   (void) const { return status_; }

protected:

private:
  const BMF_Variable* var_;
  const BMF_Command*  cmd_;
        std::string   word_;
        std::string   args_;
        std::string   result_;
        int           status_;
};

class BMF_Command {
public:
  virtual BMF_CommandResult execute (const char* szArgs) = 0;

  virtual const char* getHelp            (void) { return "No Help Available"; }

  virtual int         getNumArgs         (void) { return 0; }
  virtual int         getNumOptionalArgs (void) { return 0; }
  virtual int         getNumRequiredArgs (void) {
    return getNumArgs () - getNumOptionalArgs ();
  }

protected:
private:
};

class BMF_Variable
{
  friend class BMF_iVariableListener;
public:
  enum VariableType {
    Float,
    Double,
    Boolean,
    Byte,
    Short,
    Int,
    LongInt,
    String,

    NUM_VAR_TYPES_,

    Unknown
  } VariableTypes;

  virtual VariableType  getType        (void) const = 0;
  virtual std::string   getValueString (void) const = 0;

protected:
  VariableType type_;
};

class BMF_iVariableListener
{
public:
  virtual bool OnVarChange (BMF_Variable* var, void* val = NULL) = 0;
protected:
};

template <typename T>
class BMF_VarStub : public BMF_Variable
{
  friend class BMF_iVariableListener;
public:
  BMF_VarStub (void) : type_     (Unknown),
                       var_      (NULL),
                       listener_ (NULL)     { };

  BMF_VarStub ( T*                     var,
                BMF_iVariableListener* pListener = NULL );

  BMF_Variable::VariableType getType (void) const
  {
    return type_;
  }

  virtual std::string getValueString (void) const { return "(null)"; }

  const T& getValue (void) const { return *var_; }
  void     setValue (T& val)     {
    if (listener_ != NULL)
      listener_->OnVarChange (this, &val);
    else
      *var_ = val;
  }

  /// NOTE: Avoid doing this, as much as possible...
  T* getValuePtr (void) { return var_; }

  typedef  T _Tp;

protected:
  typename BMF_VarStub::_Tp* var_;

private:
  BMF_iVariableListener*     listener_;
};

#define BMF_CaseAdjust(ch,lower) ((lower) ? ::tolower ((int)(ch)) : (ch))

// Hash function for UTF8 strings
template < class _Kty, class _Pr = std::less <_Kty> >
class str_hash_compare
{
public:
  typedef typename _Kty::value_type value_type;
  typedef typename _Kty::size_type  size_type;  /* Was originally size_t ... */

  enum
  {
    bucket_size = 4,
    min_buckets = 8
  };

  str_hash_compare (void)      : comp ()      { };
  str_hash_compare (_Pr _Pred) : comp (_Pred) { };

  size_type operator() (const _Kty& _Keyval) const;
  bool      operator() (const _Kty& _Keyval1, const _Kty& _Keyval2) const;

  size_type hash_string (const _Kty& _Keyval) const;

private:
  _Pr comp;
};

typedef std::pair <std::string, BMF_Command*>  BMF_CommandRecord;
typedef std::pair <std::string, BMF_Variable*> BMF_VariableRecord;


class BMF_CommandProcessor
{
public:
  BMF_CommandProcessor (void);

  virtual ~BMF_CommandProcessor (void)
  {
  }

  BMF_Command* FindCommand   (const char* szCommand) const;

  const BMF_Command* AddCommand    ( const char*  szCommand,
                                     BMF_Command* pCommand );
  bool               RemoveCommand ( const char* szCommand );


  const BMF_Variable* FindVariable  (const char* szVariable) const;

  const BMF_Variable* AddVariable    ( const char*   szVariable,
                                       BMF_Variable* pVariable  );
  bool                RemoveVariable ( const char*   szVariable );


  BMF_CommandResult ProcessCommandLine      (const char* szCommandLine);
  BMF_CommandResult ProcessCommandFormatted (const char* szCommandFormat, ...);


protected:
private:
  std::unordered_map < std::string, BMF_Command*,
    str_hash_compare <std::string> > commands_;
  std::unordered_map < std::string, BMF_Variable*,
    str_hash_compare <std::string> > variables_;
};


__declspec (dllexport)
BMF_CommandProcessor*
__stdcall
BMF_GetCommandProcessor (void);

#endif /* __BMF__COMMAND_H__ */