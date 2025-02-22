/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                               PPPP   SSSSS                                  %
%                               P   P  SS                                     %
%                               PPPP    SSS                                   %
%                               P         SS                                  %
%                               P      SSSSS                                  %
%                                                                             %
%                                                                             %
%                         Read/Write Postscript Format                        %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                                 July 1992                                   %
%                                                                             %
%                                                                             %
%  Copyright 1999-2015 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    http://www.imagemagick.org/script/license.php                            %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
*/

/*
  Include declarations.
*/
#include "MagickCore/studio.h"
#include "MagickCore/artifact.h"
#include "MagickCore/attribute.h"
#include "MagickCore/blob.h"
#include "MagickCore/blob-private.h"
#include "MagickCore/cache.h"
#include "MagickCore/color.h"
#include "MagickCore/color-private.h"
#include "MagickCore/colorspace.h"
#include "MagickCore/colorspace-private.h"
#include "MagickCore/constitute.h"
#include "MagickCore/delegate.h"
#include "MagickCore/delegate-private.h"
#include "MagickCore/draw.h"
#include "MagickCore/exception.h"
#include "MagickCore/exception-private.h"
#include "MagickCore/geometry.h"
#include "MagickCore/image.h"
#include "MagickCore/image-private.h"
#include "MagickCore/list.h"
#include "MagickCore/magick.h"
#include "MagickCore/memory_.h"
#include "MagickCore/monitor.h"
#include "MagickCore/monitor-private.h"
#include "MagickCore/nt-base-private.h"
#include "MagickCore/option.h"
#include "MagickCore/profile.h"
#include "MagickCore/resource_.h"
#include "MagickCore/pixel-accessor.h"
#include "MagickCore/property.h"
#include "MagickCore/quantum-private.h"
#include "MagickCore/static.h"
#include "MagickCore/string_.h"
#include "MagickCore/module.h"
#include "MagickCore/token.h"
#include "MagickCore/transform.h"
#include "MagickCore/utility.h"

/*
  Forward declarations.
*/
static MagickBooleanType
  WritePSImage(const ImageInfo *,Image *,ExceptionInfo *);

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   I n v o k e P o s t s r i p t D e l e g a t e                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  InvokePostscriptDelegate() executes the Postscript interpreter with the
%  specified command.
%
%  The format of the InvokePostscriptDelegate method is:
%
%      MagickBooleanType InvokePostscriptDelegate(
%        const MagickBooleanType verbose,const char *command,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o verbose: A value other than zero displays the command prior to
%      executing it.
%
%    o command: the address of a character string containing the command to
%      execute.
%
%    o exception: return any errors or warnings in this structure.
%
*/
#if defined(MAGICKCORE_GS_DELEGATE) || defined(MAGICKCORE_WINDOWS_SUPPORT)
static int MagickDLLCall PostscriptDelegateMessage(void *handle,
  const char *message,int length)
{
  char
    **messages;

  ssize_t
    offset;

  offset=0;
  messages=(char **) handle;
  if (*messages == (char *) NULL)
    *messages=(char *) AcquireQuantumMemory(length+1,sizeof(char *));
  else
    {
      offset=strlen(*messages);
      *messages=(char *) ResizeQuantumMemory(*messages,offset+length+1,
        sizeof(char *));
    }
  (void) memcpy(*messages+offset,message,length);
  (*messages)[length+offset] ='\0';
  return(length);
}
#endif

static MagickBooleanType InvokePostscriptDelegate(
  const MagickBooleanType verbose,const char *command,char *message,
  ExceptionInfo *exception)
{
  int
    status;

#if defined(MAGICKCORE_GS_DELEGATE) || defined(MAGICKCORE_WINDOWS_SUPPORT)
#define SetArgsStart(command,args_start) \
  if (args_start == (const char *) NULL) \
    { \
      if (*command != '"') \
        args_start=strchr(command,' '); \
      else \
        { \
          args_start=strchr(command+1,'"'); \
          if (args_start != (const char *) NULL) \
            args_start++; \
        } \
    }

#define ExecuteGhostscriptCommand(command,status) \
{ \
  status=ExternalDelegateCommand(MagickFalse,verbose,command,message, \
    exception); \
  if (status == 0) \
    return(MagickTrue); \
  if (status < 0) \
    return(MagickFalse); \
  (void) ThrowMagickException(exception,GetMagickModule(),DelegateError, \
    "FailedToExecuteCommand","`%s' (%d)",command,status); \
  return(MagickFalse); \
}

  char
    **argv,
    *errors;

  const char
    *args_start = (const char *) NULL;

  const GhostInfo
    *ghost_info;

  gs_main_instance
    *interpreter;

  gsapi_revision_t
    revision;

  int
    argc,
    code;

  register ssize_t
    i;

#if defined(MAGICKCORE_WINDOWS_SUPPORT)
  ghost_info=NTGhostscriptDLLVectors();
#else
  GhostInfo
    ghost_info_struct;

  ghost_info=(&ghost_info_struct);
  (void) ResetMagickMemory(&ghost_info_struct,0,sizeof(ghost_info_struct));
  ghost_info_struct.delete_instance=(void (*)(gs_main_instance *))
    gsapi_delete_instance;
  ghost_info_struct.exit=(int (*)(gs_main_instance *)) gsapi_exit;
  ghost_info_struct.new_instance=(int (*)(gs_main_instance **,void *))
    gsapi_new_instance;
  ghost_info_struct.init_with_args=(int (*)(gs_main_instance *,int,char **))
    gsapi_init_with_args;
  ghost_info_struct.run_string=(int (*)(gs_main_instance *,const char *,int,
    int *)) gsapi_run_string;
  ghost_info_struct.set_stdio=(int (*)(gs_main_instance *,int(*)(void *,char *,
    int),int(*)(void *,const char *,int),int(*)(void *, const char *, int)))
    gsapi_set_stdio;
  ghost_info_struct.revision=(int (*)(gsapi_revision_t *,int)) gsapi_revision;
#endif
  if (ghost_info == (GhostInfo *) NULL)
    ExecuteGhostscriptCommand(command,status);
  if ((ghost_info->revision)(&revision,sizeof(revision)) != 0)
    revision.revision=0;
  if (verbose != MagickFalse)
    {
      (void) fprintf(stdout,"[ghostscript library %.2f]",(double)
        revision.revision/100.0);
      SetArgsStart(command,args_start);
      (void) fputs(args_start,stdout);
    }
  errors=(char *) NULL;
  status=(ghost_info->new_instance)(&interpreter,(void *) &errors);
  if (status < 0)
    ExecuteGhostscriptCommand(command,status);
  code=0;
  argv=StringToArgv(command,&argc);
  if (argv == (char **) NULL)
    {
      (ghost_info->delete_instance)(interpreter);
      return(MagickFalse);
    }
  (void) (ghost_info->set_stdio)(interpreter,(int(MagickDLLCall *)(void *,
    char *,int)) NULL,PostscriptDelegateMessage,PostscriptDelegateMessage);
  status=(ghost_info->init_with_args)(interpreter,argc-1,argv+1);
  if (status == 0)
    status=(ghost_info->run_string)(interpreter,"systemdict /start get exec\n",
      0,&code);
  (ghost_info->exit)(interpreter);
  (ghost_info->delete_instance)(interpreter);
  for (i=0; i < (ssize_t) argc; i++)
    argv[i]=DestroyString(argv[i]);
  argv=(char **) RelinquishMagickMemory(argv);
  if (status != 0)
    {
      SetArgsStart(command,args_start);
      if (status == -101) /* quit */
        (void) FormatLocaleString(message,MagickPathExtent,
          "[ghostscript library %.2f]%s: %s",(double)revision.revision / 100,
          args_start,errors);
      else
        {
          (void) ThrowMagickException(exception,GetMagickModule(),
            DelegateError,"PostscriptDelegateFailed",
            "`[ghostscript library %.2f]%s': %s",
            (double)revision.revision / 100,args_start,errors);
          if (errors != (char *) NULL)
            errors=DestroyString(errors);
          (void) LogMagickEvent(CoderEvent,GetMagickModule(),
            "Ghostscript returns status %d, exit code %d",status,code);
          return(MagickFalse);
        }
    }
  if (errors != (char *) NULL)
    errors=DestroyString(errors);
  return(MagickTrue);
#else
  status=ExternalDelegateCommand(MagickFalse,verbose,command,message,exception);
  return(status == 0 ? MagickTrue : MagickFalse);
#endif
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   I s P S                                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  IsPS() returns MagickTrue if the image format type, identified by the
%  magick string, is PS.
%
%  The format of the IsPS method is:
%
%      MagickBooleanType IsPS(const unsigned char *magick,const size_t length)
%
%  A description of each parameter follows:
%
%    o magick: compare image format pattern against these bytes.
%
%    o length: Specifies the length of the magick string.
%
*/
static MagickBooleanType IsPS(const unsigned char *magick,const size_t length)
{
  if (length < 4)
    return(MagickFalse);
  if (memcmp(magick,"%!",2) == 0)
    return(MagickTrue);
  if (memcmp(magick,"\004%!",3) == 0)
    return(MagickTrue);
  return(MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e a d P S I m a g e                                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReadPSImage() reads a Postscript image file and returns it.  It allocates
%  the memory necessary for the new Image structure and returns a pointer
%  to the new image.
%
%  The format of the ReadPSImage method is:
%
%      Image *ReadPSImage(const ImageInfo *image_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image_info: the image info.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static MagickBooleanType IsPostscriptRendered(const char *path)
{
  MagickBooleanType
    status;

  struct stat
    attributes;

  if ((path == (const char *) NULL) || (*path == '\0'))
    return(MagickFalse);
  status=GetPathAttributes(path,&attributes);
  if ((status != MagickFalse) && S_ISREG(attributes.st_mode) &&
      (attributes.st_size > 0))
    return(MagickTrue);
  return(MagickFalse);
}

static inline int ProfileInteger(Image *image,short int *hex_digits)
{
  int
    c,
    l,
    value;

  register ssize_t
    i;

  l=0;
  value=0;
  for (i=0; i < 2; )
  {
    c=ReadBlobByte(image);
    if ((c == EOF) || ((c == '%') && (l == '%')))
      {
        value=(-1);
        break;
      }
    l=c;
    c&=0xff;
    if (isxdigit(c) == MagickFalse)
      continue;
    value=(int) ((size_t) value << 4)+hex_digits[c];
    i++;
  }
  return(value);
}

static Image *ReadPSImage(const ImageInfo *image_info,ExceptionInfo *exception)
{
#define BoundingBox  "BoundingBox:"
#define BeginDocument  "BeginDocument:"
#define BeginXMPPacket  "<?xpacket begin="
#define EndXMPPacket  "<?xpacket end="
#define ICCProfile "BeginICCProfile:"
#define CMYKCustomColor  "CMYKCustomColor:"
#define CMYKProcessColor  "CMYKProcessColor:"
#define DocumentMedia  "DocumentMedia:"
#define DocumentCustomColors  "DocumentCustomColors:"
#define DocumentProcessColors  "DocumentProcessColors:"
#define EndDocument  "EndDocument:"
#define HiResBoundingBox  "HiResBoundingBox:"
#define ImageData  "ImageData:"
#define PageBoundingBox  "PageBoundingBox:"
#define LanguageLevel  "LanguageLevel:"
#define PageMedia  "PageMedia:"
#define Pages  "Pages:"
#define PhotoshopProfile  "BeginPhotoshop:"
#define PostscriptLevel  "!PS-"
#define RenderPostscriptText  "  Rendering Postscript...  "
#define SpotColor  "+ "

  char
    command[MagickPathExtent],
    *density,
    filename[MagickPathExtent],
    geometry[MagickPathExtent],
    input_filename[MagickPathExtent],
    message[MagickPathExtent],
    *options,
    postscript_filename[MagickPathExtent];

  const char
    *option;

  const DelegateInfo
    *delegate_info;

  GeometryInfo
    geometry_info;

  Image
    *image,
    *next,
    *postscript_image;

  ImageInfo
    *read_info;

  int
    c,
    file;

  MagickBooleanType
    cmyk,
    fitPage,
    skip,
    status;

  MagickStatusType
    flags;

  PointInfo
    delta,
    resolution;

  RectangleInfo
    page;

  register char
    *p;

  register ssize_t
    i;

  SegmentInfo
    bounds,
    hires_bounds;

  short int
    hex_digits[256];

  size_t
    length;

  ssize_t
    count,
    priority;

  StringInfo
    *profile;

  unsigned long
    columns,
    extent,
    language_level,
    pages,
    rows,
    scene,
    spotcolor;

  /*
    Open image file.
  */
  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickCoreSignature);
  if (image_info->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      image_info->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  image=AcquireImage(image_info,exception);
  status=OpenBlob(image_info,image,ReadBinaryBlobMode,exception);
  if (status == MagickFalse)
    {
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  status=AcquireUniqueSymbolicLink(image_info->filename,input_filename);
  if (status == MagickFalse)
    {
      ThrowFileException(exception,FileOpenError,"UnableToCreateTemporaryFile",
        image_info->filename);
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  /*
    Initialize hex values.
  */
  (void) ResetMagickMemory(hex_digits,0,sizeof(hex_digits));
  hex_digits[(int) '0']=0;
  hex_digits[(int) '1']=1;
  hex_digits[(int) '2']=2;
  hex_digits[(int) '3']=3;
  hex_digits[(int) '4']=4;
  hex_digits[(int) '5']=5;
  hex_digits[(int) '6']=6;
  hex_digits[(int) '7']=7;
  hex_digits[(int) '8']=8;
  hex_digits[(int) '9']=9;
  hex_digits[(int) 'a']=10;
  hex_digits[(int) 'b']=11;
  hex_digits[(int) 'c']=12;
  hex_digits[(int) 'd']=13;
  hex_digits[(int) 'e']=14;
  hex_digits[(int) 'f']=15;
  hex_digits[(int) 'A']=10;
  hex_digits[(int) 'B']=11;
  hex_digits[(int) 'C']=12;
  hex_digits[(int) 'D']=13;
  hex_digits[(int) 'E']=14;
  hex_digits[(int) 'F']=15;
  /*
    Set the page density.
  */
  delta.x=DefaultResolution;
  delta.y=DefaultResolution;
  if ((image->resolution.x == 0.0) || (image->resolution.y == 0.0))
    {
      flags=ParseGeometry(PSDensityGeometry,&geometry_info);
      image->resolution.x=geometry_info.rho;
      image->resolution.y=geometry_info.sigma;
      if ((flags & SigmaValue) == 0)
        image->resolution.y=image->resolution.x;
    }
  if (image_info->density != (char *) NULL)
    {
      flags=ParseGeometry(image_info->density,&geometry_info);
      image->resolution.x=geometry_info.rho;
      image->resolution.y=geometry_info.sigma;
      if ((flags & SigmaValue) == 0)
        image->resolution.y=image->resolution.x;
    }
  (void) ParseAbsoluteGeometry(PSPageGeometry,&page);
  if (image_info->page != (char *) NULL)
    (void) ParseAbsoluteGeometry(image_info->page,&page);
  resolution=image->resolution;
  page.width=(size_t) ceil((double) (page.width*resolution.x/delta.x)-0.5);
  page.height=(size_t) ceil((double) (page.height*resolution.y/delta.y)-0.5);
  /*
    Determine page geometry from the Postscript bounding box.
  */
  (void) ResetMagickMemory(&bounds,0,sizeof(bounds));
  (void) ResetMagickMemory(command,0,sizeof(command));
  cmyk=image_info->colorspace == CMYKColorspace ? MagickTrue : MagickFalse;
  (void) ResetMagickMemory(&hires_bounds,0,sizeof(hires_bounds));
  columns=0;
  rows=0;
  priority=0;
  rows=0;
  extent=0;
  spotcolor=0;
  language_level=1;
  pages=(~0UL);
  skip=MagickFalse;
  p=command;
  for (c=ReadBlobByte(image); c != EOF; c=ReadBlobByte(image))
  {
    /*
      Note document structuring comments.
    */
    *p++=(char) c;
    if ((strchr("\n\r%",c) == (char *) NULL) &&
        ((size_t) (p-command) < (MagickPathExtent-1)))
      continue;
    *p='\0';
    p=command;
    /*
      Skip %%BeginDocument thru %%EndDocument.
    */
    if (LocaleNCompare(BeginDocument,command,strlen(BeginDocument)) == 0)
      skip=MagickTrue;
    if (LocaleNCompare(EndDocument,command,strlen(EndDocument)) == 0)
      skip=MagickFalse;
    if (skip != MagickFalse)
      continue;
    if (LocaleNCompare(PostscriptLevel,command,strlen(PostscriptLevel)) == 0)
      {
        (void) SetImageProperty(image,"ps:Level",command+4,exception);
        if (GlobExpression(command,"*EPSF-*",MagickTrue) != MagickFalse)
          pages=1;
      }
    if (LocaleNCompare(LanguageLevel,command,strlen(LanguageLevel)) == 0)
      (void) sscanf(command,LanguageLevel " %lu",&language_level);
    if (LocaleNCompare(Pages,command,strlen(Pages)) == 0)
      (void) sscanf(command,Pages " %lu",&pages);
    if (LocaleNCompare(ImageData,command,strlen(ImageData)) == 0)
      (void) sscanf(command,ImageData " %lu %lu",&columns,&rows);
    if (LocaleNCompare(ICCProfile,command,strlen(ICCProfile)) == 0)
      {
        unsigned char
          *datum;

        /*
          Read ICC profile.
        */
        profile=AcquireStringInfo(65536);
        for (i=0; (c=ProfileInteger(image,hex_digits)) != EOF; i++)
        {
          SetStringInfoLength(profile,(size_t) i+1);
          datum=GetStringInfoDatum(profile);
          datum[i]=(unsigned char) c;
        }
        (void) SetImageProfile(image,"icc",profile,exception);
        profile=DestroyStringInfo(profile);
        continue;
      }
    if (LocaleNCompare(PhotoshopProfile,command,strlen(PhotoshopProfile)) == 0)
      {
        unsigned char
          *q;

        /*
          Read Photoshop profile.
        */
        count=(ssize_t) sscanf(command,PhotoshopProfile " %lu",&extent);
        if (count != 1)
          continue;
        length=extent;
        profile=BlobToStringInfo((const void *) NULL,length);
        if (profile != (StringInfo *) NULL)
          {
            q=GetStringInfoDatum(profile);
            for (i=0; i < (ssize_t) length; i++)
              *q++=(unsigned char) ProfileInteger(image,hex_digits);
            (void) SetImageProfile(image,"8bim",profile,exception);
            profile=DestroyStringInfo(profile);
          }
        continue;
      }
    if (LocaleNCompare(BeginXMPPacket,command,strlen(BeginXMPPacket)) == 0)
      {
        /*
          Read XMP profile.
        */
        p=command;
        profile=StringToStringInfo(command);
        for (i=GetStringInfoLength(profile)-1; c != EOF; i++)
        {
          SetStringInfoLength(profile,i+1);
          c=ReadBlobByte(image);
          GetStringInfoDatum(profile)[i]=(unsigned char) c;
          *p++=(char) c;
          if ((strchr("\n\r%",c) == (char *) NULL) &&
              ((size_t) (p-command) < (MagickPathExtent-1)))
            continue;
          *p='\0';
          p=command;
          if (LocaleNCompare(EndXMPPacket,command,strlen(EndXMPPacket)) == 0)
            break;
        }
        SetStringInfoLength(profile,i);
        (void) SetImageProfile(image,"xmp",profile,exception);
        profile=DestroyStringInfo(profile);
        continue;
      }
    /*
      Is this a CMYK document?
    */
    length=strlen(DocumentProcessColors);
    if (LocaleNCompare(DocumentProcessColors,command,length) == 0)
      {
        if ((GlobExpression(command,"*Cyan*",MagickTrue) != MagickFalse) ||
            (GlobExpression(command,"*Magenta*",MagickTrue) != MagickFalse) ||
            (GlobExpression(command,"*Yellow*",MagickTrue) != MagickFalse))
          cmyk=MagickTrue;
      }
    if (LocaleNCompare(CMYKCustomColor,command,strlen(CMYKCustomColor)) == 0)
      cmyk=MagickTrue;
    if (LocaleNCompare(CMYKProcessColor,command,strlen(CMYKProcessColor)) == 0)
      cmyk=MagickTrue;
    length=strlen(DocumentCustomColors);
    if ((LocaleNCompare(DocumentCustomColors,command,length) == 0) ||
        (LocaleNCompare(CMYKCustomColor,command,strlen(CMYKCustomColor)) == 0) ||
        (LocaleNCompare(SpotColor,command,strlen(SpotColor)) == 0))
      {
        char
          property[MagickPathExtent],
          *value;

        register char
          *q;

        /*
          Note spot names.
        */
        (void) FormatLocaleString(property,MagickPathExtent,"ps:SpotColor-%.20g",
          (double) (spotcolor++));
        for (q=command; *q != '\0'; q++)
          if (isspace((int) (unsigned char) *q) != 0)
            break;
        value=AcquireString(q);
        (void) SubstituteString(&value,"(","");
        (void) SubstituteString(&value,")","");
        (void) StripString(value);
        (void) SetImageProperty(image,property,value,exception);
        value=DestroyString(value);
        continue;
      }
    if (image_info->page != (char *) NULL)
      continue;
    /*
      Note region defined by bounding box.
    */
    count=0;
    i=0;
    if (LocaleNCompare(BoundingBox,command,strlen(BoundingBox)) == 0)
      {
        count=(ssize_t) sscanf(command,BoundingBox " %lf %lf %lf %lf",
          &bounds.x1,&bounds.y1,&bounds.x2,&bounds.y2);
        i=2;
      }
    if (LocaleNCompare(DocumentMedia,command,strlen(DocumentMedia)) == 0)
      {
        count=(ssize_t) sscanf(command,DocumentMedia " %lf %lf %lf %lf",
          &bounds.x1,&bounds.y1,&bounds.x2,&bounds.y2);
        i=1;
      }
    if (LocaleNCompare(HiResBoundingBox,command,strlen(HiResBoundingBox)) == 0)
      {
        count=(ssize_t) sscanf(command,HiResBoundingBox " %lf %lf %lf %lf",
          &bounds.x1,&bounds.y1,&bounds.x2,&bounds.y2);
        i=3;
      }
    if (LocaleNCompare(PageBoundingBox,command,strlen(PageBoundingBox)) == 0)
      {
        count=(ssize_t) sscanf(command,PageBoundingBox " %lf %lf %lf %lf",
          &bounds.x1,&bounds.y1,&bounds.x2,&bounds.y2);
        i=1;
      }
    if (LocaleNCompare(PageMedia,command,strlen(PageMedia)) == 0)
      {
        count=(ssize_t) sscanf(command,PageMedia " %lf %lf %lf %lf",
          &bounds.x1,&bounds.y1,&bounds.x2,&bounds.y2);
        i=1;
      }
    if ((count != 4) || (i < (ssize_t) priority))
      continue;
    if ((fabs(bounds.x2-bounds.x1) <= fabs(hires_bounds.x2-hires_bounds.x1)) ||
        (fabs(bounds.y2-bounds.y1) <= fabs(hires_bounds.y2-hires_bounds.y1)))
      if (i ==  (ssize_t) priority)
        continue;
    hires_bounds=bounds;
    priority=i;
  }
  if ((fabs(hires_bounds.x2-hires_bounds.x1) >= MagickEpsilon) && 
      (fabs(hires_bounds.y2-hires_bounds.y1) >= MagickEpsilon))
    {
      /*
        Set Postscript render geometry.
      */
      (void) FormatLocaleString(geometry,MagickPathExtent,"%gx%g%+.15g%+.15g",
        hires_bounds.x2-hires_bounds.x1,hires_bounds.y2-hires_bounds.y1,
        hires_bounds.x1,hires_bounds.y1);
      (void) SetImageProperty(image,"ps:HiResBoundingBox",geometry,exception);
      page.width=(size_t) ceil((double) ((hires_bounds.x2-hires_bounds.x1)*
        resolution.x/delta.x)-0.5);
      page.height=(size_t) ceil((double) ((hires_bounds.y2-hires_bounds.y1)*
        resolution.y/delta.y)-0.5);
    }
  fitPage=MagickFalse;
  option=GetImageOption(image_info,"eps:fit-page");
  if (option != (char *) NULL)
  {
    char
      *page_geometry;

    page_geometry=GetPageGeometry(option);
    flags=ParseMetaGeometry(page_geometry,&page.x,&page.y,&page.width,
      &page.height);
    if (flags == NoValue)
      {
        (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
          "InvalidGeometry","`%s'",option);
        image=DestroyImage(image);
        return((Image *) NULL);
      }
    page.width=(size_t) ceil((double) (page.width*image->resolution.x/delta.x)
      -0.5);
    page.height=(size_t) ceil((double) (page.height*image->resolution.y/
      delta.y) -0.5);
    page_geometry=DestroyString(page_geometry);
    fitPage=MagickTrue;
  }
  (void) CloseBlob(image);
  if (IssRGBCompatibleColorspace(image_info->colorspace) != MagickFalse)
    cmyk=MagickFalse;
  /*
    Create Ghostscript control file.
  */
  file=AcquireUniqueFileResource(postscript_filename);
  if (file == -1)
    {
      ThrowFileException(exception,FileOpenError,"UnableToOpenFile",
        image_info->filename);
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  (void) CopyMagickString(command,"/setpagedevice {pop} bind 1 index where {"
    "dup wcheck {3 1 roll put} {pop def} ifelse} {def} ifelse\n"
    "<</UseCIEColor true>>setpagedevice\n",MagickPathExtent);
  count=write(file,command,(unsigned int) strlen(command));
  if (image_info->page == (char *) NULL)
    {
      char
        translate_geometry[MagickPathExtent];

      (void) FormatLocaleString(translate_geometry,MagickPathExtent,
        "%g %g translate\n",-bounds.x1,-bounds.y1);
      count=write(file,translate_geometry,(unsigned int)
        strlen(translate_geometry));
    }
  file=close(file)-1;
  /*
    Render Postscript with the Ghostscript delegate.
  */
  if (image_info->monochrome != MagickFalse)
    delegate_info=GetDelegateInfo("ps:mono",(char *) NULL,exception);
  else
    if (cmyk != MagickFalse)
      delegate_info=GetDelegateInfo("ps:cmyk",(char *) NULL,exception);
    else
      delegate_info=GetDelegateInfo("ps:alpha",(char *) NULL,exception);
  if (delegate_info == (const DelegateInfo *) NULL)
    {
      (void) RelinquishUniqueFileResource(postscript_filename);
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  density=AcquireString("");
  options=AcquireString("");
  (void) FormatLocaleString(density,MagickPathExtent,"%gx%g",resolution.x,
    resolution.y);
  (void) FormatLocaleString(options,MagickPathExtent,"-g%.20gx%.20g ",(double)
    page.width,(double) page.height);
  read_info=CloneImageInfo(image_info);
  *read_info->magick='\0';
  if (read_info->number_scenes != 0)
    {
      char
        pages[MagickPathExtent];

      (void) FormatLocaleString(pages,MagickPathExtent,"-dFirstPage=%.20g "
        "-dLastPage=%.20g ",(double) read_info->scene+1,(double)
        (read_info->scene+read_info->number_scenes));
      (void) ConcatenateMagickString(options,pages,MagickPathExtent);
      read_info->number_scenes=0;
      if (read_info->scenes != (char *) NULL)
        *read_info->scenes='\0';
    }
  if (*image_info->magick == 'E')
    {
      option=GetImageOption(image_info,"eps:use-cropbox");
      if ((option == (const char *) NULL) ||
          (IsStringTrue(option) != MagickFalse))
        (void) ConcatenateMagickString(options,"-dEPSCrop ",MagickPathExtent);
      if (fitPage != MagickFalse)
        (void) ConcatenateMagickString(options,"-dEPSFitPage ",MagickPathExtent);
    }
  (void) CopyMagickString(filename,read_info->filename,MagickPathExtent);
  (void) AcquireUniqueFilename(filename);
  (void) RelinquishUniqueFileResource(filename);
  (void) ConcatenateMagickString(filename,"%d",MagickPathExtent);
  (void) FormatLocaleString(command,MagickPathExtent,
    GetDelegateCommands(delegate_info),
    read_info->antialias != MagickFalse ? 4 : 1,
    read_info->antialias != MagickFalse ? 4 : 1,density,options,filename,
    postscript_filename,input_filename);
  options=DestroyString(options);
  density=DestroyString(density);
  *message='\0';
  status=InvokePostscriptDelegate(read_info->verbose,command,message,exception);
  (void) InterpretImageFilename(image_info,image,filename,1,
    read_info->filename,exception);
  if ((status == MagickFalse) ||
      (IsPostscriptRendered(read_info->filename) == MagickFalse))
    {
      (void) ConcatenateMagickString(command," -c showpage",MagickPathExtent);
      status=InvokePostscriptDelegate(read_info->verbose,command,message,
        exception);
    }
  (void) RelinquishUniqueFileResource(postscript_filename);
  (void) RelinquishUniqueFileResource(input_filename);
  postscript_image=(Image *) NULL;
  if (status == MagickFalse)
    for (i=1; ; i++)
    {
      (void) InterpretImageFilename(image_info,image,filename,(int) i,
        read_info->filename,exception);
      if (IsPostscriptRendered(read_info->filename) == MagickFalse)
        break;
      (void) RelinquishUniqueFileResource(read_info->filename);
    }
  else
    for (i=1; ; i++)
    {
      (void) InterpretImageFilename(image_info,image,filename,(int) i,
        read_info->filename,exception);
      if (IsPostscriptRendered(read_info->filename) == MagickFalse)
        break;
      read_info->blob=NULL;
      read_info->length=0;
      next=ReadImage(read_info,exception);
      (void) RelinquishUniqueFileResource(read_info->filename);
      if (next == (Image *) NULL)
        break;
      AppendImageToList(&postscript_image,next);
    }
  (void) RelinquishUniqueFileResource(read_info->filename);
  read_info=DestroyImageInfo(read_info);
  if (postscript_image == (Image *) NULL)
    {
      if (*message != '\0')
        (void) ThrowMagickException(exception,GetMagickModule(),
          DelegateError,"PostscriptDelegateFailed","`%s'",message);
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  if (LocaleCompare(postscript_image->magick,"BMP") == 0)
    {
      Image
        *cmyk_image;

      cmyk_image=ConsolidateCMYKImages(postscript_image,exception);
      if (cmyk_image != (Image *) NULL)
        {
          postscript_image=DestroyImageList(postscript_image);
          postscript_image=cmyk_image;
        }
    }
  if (image_info->number_scenes != 0)
    {
      Image
        *clone_image;

      /*
        Add place holder images to meet the subimage specification requirement.
      */
      for (i=0; i < (ssize_t) image_info->scene; i++)
      {
        clone_image=CloneImage(postscript_image,1,1,MagickTrue,exception);
        if (clone_image != (Image *) NULL)
          PrependImageToList(&postscript_image,clone_image);
      }
    }
  do
  {
    (void) CopyMagickString(postscript_image->filename,filename,MagickPathExtent);
    (void) CopyMagickString(postscript_image->magick,image->magick,
      MagickPathExtent);
    if (columns != 0)
      postscript_image->magick_columns=columns;
    if (rows != 0)
      postscript_image->magick_rows=rows;
    postscript_image->page=page;
    (void) CloneImageProfiles(postscript_image,image);
    (void) CloneImageProperties(postscript_image,image);
    next=SyncNextImageInList(postscript_image);
    if (next != (Image *) NULL)
      postscript_image=next;
  } while (next != (Image *) NULL);
  image=DestroyImageList(image);
  scene=0;
  for (next=GetFirstImageInList(postscript_image); next != (Image *) NULL; )
  {
    next->scene=scene++;
    next=GetNextImageInList(next);
  }
  return(GetFirstImageInList(postscript_image));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e g i s t e r P S I m a g e                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RegisterPSImage() adds properties for the PS image format to
%  the list of supported formats.  The properties include the image format
%  tag, a method to read and/or write the format, whether the format
%  supports the saving of more than one frame to the same file or blob,
%  whether the format supports native in-memory I/O, and a brief
%  description of the format.
%
%  The format of the RegisterPSImage method is:
%
%      size_t RegisterPSImage(void)
%
*/
ModuleExport size_t RegisterPSImage(void)
{
  MagickInfo
    *entry;

  entry=AcquireMagickInfo("PS","EPI",
    "Encapsulated PostScript Interchange format");
  entry->decoder=(DecodeImageHandler *) ReadPSImage;
  entry->encoder=(EncodeImageHandler *) WritePSImage;
  entry->magick=(IsImageFormatHandler *) IsPS;
  entry->flags^=CoderAdjoinFlag;
  entry->flags^=CoderBlobSupportFlag;
  entry->flags|=CoderSeekableStreamFlag;
  entry->mime_type=ConstantString("application/postscript");
  (void) RegisterMagickInfo(entry);
  entry=AcquireMagickInfo("PS","EPS","Encapsulated PostScript");
  entry->decoder=(DecodeImageHandler *) ReadPSImage;
  entry->encoder=(EncodeImageHandler *) WritePSImage;
  entry->magick=(IsImageFormatHandler *) IsPS;
  entry->flags^=CoderAdjoinFlag;
  entry->flags^=CoderBlobSupportFlag;
  entry->flags|=CoderSeekableStreamFlag;
  entry->mime_type=ConstantString("application/postscript");
  (void) RegisterMagickInfo(entry);
  entry=AcquireMagickInfo("PS","EPSF","Encapsulated PostScript");
  entry->decoder=(DecodeImageHandler *) ReadPSImage;
  entry->encoder=(EncodeImageHandler *) WritePSImage;
  entry->magick=(IsImageFormatHandler *) IsPS;
  entry->flags^=CoderAdjoinFlag;
  entry->flags^=CoderBlobSupportFlag;
  entry->flags|=CoderSeekableStreamFlag;
  entry->mime_type=ConstantString("application/postscript");
  (void) RegisterMagickInfo(entry);
  entry=AcquireMagickInfo("PS","EPSI",
    "Encapsulated PostScript Interchange format");
  entry->decoder=(DecodeImageHandler *) ReadPSImage;
  entry->encoder=(EncodeImageHandler *) WritePSImage;
  entry->magick=(IsImageFormatHandler *) IsPS;
  entry->flags^=CoderAdjoinFlag;
  entry->flags^=CoderBlobSupportFlag;
  entry->flags|=CoderSeekableStreamFlag;
  entry->mime_type=ConstantString("application/postscript");
  (void) RegisterMagickInfo(entry);
  entry=AcquireMagickInfo("PS","PS","PostScript");
  entry->decoder=(DecodeImageHandler *) ReadPSImage;
  entry->encoder=(EncodeImageHandler *) WritePSImage;
  entry->magick=(IsImageFormatHandler *) IsPS;
  entry->mime_type=ConstantString("application/postscript");
  entry->flags^=CoderBlobSupportFlag;
  entry->flags|=CoderSeekableStreamFlag;
  (void) RegisterMagickInfo(entry);
  return(MagickImageCoderSignature);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   U n r e g i s t e r P S I m a g e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  UnregisterPSImage() removes format registrations made by the
%  PS module from the list of supported formats.
%
%  The format of the UnregisterPSImage method is:
%
%      UnregisterPSImage(void)
%
*/
ModuleExport void UnregisterPSImage(void)
{
  (void) UnregisterMagickInfo("EPI");
  (void) UnregisterMagickInfo("EPS");
  (void) UnregisterMagickInfo("EPSF");
  (void) UnregisterMagickInfo("EPSI");
  (void) UnregisterMagickInfo("PS");
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   W r i t e P S I m a g e                                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WritePSImage translates an image to encapsulated Postscript
%  Level I for printing.  If the supplied geometry is null, the image is
%  centered on the Postscript page.  Otherwise, the image is positioned as
%  specified by the geometry.
%
%  The format of the WritePSImage method is:
%
%      MagickBooleanType WritePSImage(const ImageInfo *image_info,
%        Image *image,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image_info: the image info.
%
%    o image: the image.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static inline unsigned char *PopHexPixel(const char *const *hex_digits,
  const size_t pixel,unsigned char *pixels)
{
  register const char
    *hex;

  hex=hex_digits[pixel];
  *pixels++=(unsigned char) (*hex++);
  *pixels++=(unsigned char) (*hex);
  return(pixels);
}

static MagickBooleanType WritePSImage(const ImageInfo *image_info,Image *image,
  ExceptionInfo *exception)
{
#define WriteRunlengthPacket(image,pixel,length,p) \
{ \
  if ((image->alpha_trait != UndefinedPixelTrait) && \
      (GetPixelAlpha(image,p) == (Quantum) TransparentAlpha)) \
    { \
      q=PopHexPixel(hex_digits,0xff,q); \
      q=PopHexPixel(hex_digits,0xff,q); \
      q=PopHexPixel(hex_digits,0xff,q); \
    } \
  else \
    { \
      q=PopHexPixel(hex_digits,ScaleQuantumToChar(ClampToQuantum(pixel.red)),q); \
      q=PopHexPixel(hex_digits,ScaleQuantumToChar(ClampToQuantum(pixel.green)),q); \
      q=PopHexPixel(hex_digits,ScaleQuantumToChar(ClampToQuantum(pixel.blue)),q); \
    } \
  q=PopHexPixel(hex_digits,(size_t) MagickMin(length,0xff),q); \
}

  static const char
    *const hex_digits[] =
    {
      "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0A", "0B",
      "0C", "0D", "0E", "0F", "10", "11", "12", "13", "14", "15", "16", "17",
      "18", "19", "1A", "1B", "1C", "1D", "1E", "1F", "20", "21", "22", "23",
      "24", "25", "26", "27", "28", "29", "2A", "2B", "2C", "2D", "2E", "2F",
      "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3A", "3B",
      "3C", "3D", "3E", "3F", "40", "41", "42", "43", "44", "45", "46", "47",
      "48", "49", "4A", "4B", "4C", "4D", "4E", "4F", "50", "51", "52", "53",
      "54", "55", "56", "57", "58", "59", "5A", "5B", "5C", "5D", "5E", "5F",
      "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6A", "6B",
      "6C", "6D", "6E", "6F", "70", "71", "72", "73", "74", "75", "76", "77",
      "78", "79", "7A", "7B", "7C", "7D", "7E", "7F", "80", "81", "82", "83",
      "84", "85", "86", "87", "88", "89", "8A", "8B", "8C", "8D", "8E", "8F",
      "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9A", "9B",
      "9C", "9D", "9E", "9F", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
      "A8", "A9", "AA", "AB", "AC", "AD", "AE", "AF", "B0", "B1", "B2", "B3",
      "B4", "B5", "B6", "B7", "B8", "B9", "BA", "BB", "BC", "BD", "BE", "BF",
      "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "CA", "CB",
      "CC", "CD", "CE", "CF", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
      "D8", "D9", "DA", "DB", "DC", "DD", "DE", "DF", "E0", "E1", "E2", "E3",
      "E4", "E5", "E6", "E7", "E8", "E9", "EA", "EB", "EC", "ED", "EE", "EF",
      "F0", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "FA", "FB",
      "FC", "FD", "FE", "FF",  (const char *) NULL
    },
    *const PostscriptProlog[]=
    {
      "%%BeginProlog",
      "%",
      "% Display a color image.  The image is displayed in color on",
      "% Postscript viewers or printers that support color, otherwise",
      "% it is displayed as grayscale.",
      "%",
      "/DirectClassPacket",
      "{",
      "  %",
      "  % Get a DirectClass packet.",
      "  %",
      "  % Parameters:",
      "  %   red.",
      "  %   green.",
      "  %   blue.",
      "  %   length: number of pixels minus one of this color (optional).",
      "  %",
      "  currentfile color_packet readhexstring pop pop",
      "  compression 0 eq",
      "  {",
      "    /number_pixels 3 def",
      "  }",
      "  {",
      "    currentfile byte readhexstring pop 0 get",
      "    /number_pixels exch 1 add 3 mul def",
      "  } ifelse",
      "  0 3 number_pixels 1 sub",
      "  {",
      "    pixels exch color_packet putinterval",
      "  } for",
      "  pixels 0 number_pixels getinterval",
      "} bind def",
      "",
      "/DirectClassImage",
      "{",
      "  %",
      "  % Display a DirectClass image.",
      "  %",
      "  systemdict /colorimage known",
      "  {",
      "    columns rows 8",
      "    [",
      "      columns 0 0",
      "      rows neg 0 rows",
      "    ]",
      "    { DirectClassPacket } false 3 colorimage",
      "  }",
      "  {",
      "    %",
      "    % No colorimage operator;  convert to grayscale.",
      "    %",
      "    columns rows 8",
      "    [",
      "      columns 0 0",
      "      rows neg 0 rows",
      "    ]",
      "    { GrayDirectClassPacket } image",
      "  } ifelse",
      "} bind def",
      "",
      "/GrayDirectClassPacket",
      "{",
      "  %",
      "  % Get a DirectClass packet;  convert to grayscale.",
      "  %",
      "  % Parameters:",
      "  %   red",
      "  %   green",
      "  %   blue",
      "  %   length: number of pixels minus one of this color (optional).",
      "  %",
      "  currentfile color_packet readhexstring pop pop",
      "  color_packet 0 get 0.299 mul",
      "  color_packet 1 get 0.587 mul add",
      "  color_packet 2 get 0.114 mul add",
      "  cvi",
      "  /gray_packet exch def",
      "  compression 0 eq",
      "  {",
      "    /number_pixels 1 def",
      "  }",
      "  {",
      "    currentfile byte readhexstring pop 0 get",
      "    /number_pixels exch 1 add def",
      "  } ifelse",
      "  0 1 number_pixels 1 sub",
      "  {",
      "    pixels exch gray_packet put",
      "  } for",
      "  pixels 0 number_pixels getinterval",
      "} bind def",
      "",
      "/GrayPseudoClassPacket",
      "{",
      "  %",
      "  % Get a PseudoClass packet;  convert to grayscale.",
      "  %",
      "  % Parameters:",
      "  %   index: index into the colormap.",
      "  %   length: number of pixels minus one of this color (optional).",
      "  %",
      "  currentfile byte readhexstring pop 0 get",
      "  /offset exch 3 mul def",
      "  /color_packet colormap offset 3 getinterval def",
      "  color_packet 0 get 0.299 mul",
      "  color_packet 1 get 0.587 mul add",
      "  color_packet 2 get 0.114 mul add",
      "  cvi",
      "  /gray_packet exch def",
      "  compression 0 eq",
      "  {",
      "    /number_pixels 1 def",
      "  }",
      "  {",
      "    currentfile byte readhexstring pop 0 get",
      "    /number_pixels exch 1 add def",
      "  } ifelse",
      "  0 1 number_pixels 1 sub",
      "  {",
      "    pixels exch gray_packet put",
      "  } for",
      "  pixels 0 number_pixels getinterval",
      "} bind def",
      "",
      "/PseudoClassPacket",
      "{",
      "  %",
      "  % Get a PseudoClass packet.",
      "  %",
      "  % Parameters:",
      "  %   index: index into the colormap.",
      "  %   length: number of pixels minus one of this color (optional).",
      "  %",
      "  currentfile byte readhexstring pop 0 get",
      "  /offset exch 3 mul def",
      "  /color_packet colormap offset 3 getinterval def",
      "  compression 0 eq",
      "  {",
      "    /number_pixels 3 def",
      "  }",
      "  {",
      "    currentfile byte readhexstring pop 0 get",
      "    /number_pixels exch 1 add 3 mul def",
      "  } ifelse",
      "  0 3 number_pixels 1 sub",
      "  {",
      "    pixels exch color_packet putinterval",
      "  } for",
      "  pixels 0 number_pixels getinterval",
      "} bind def",
      "",
      "/PseudoClassImage",
      "{",
      "  %",
      "  % Display a PseudoClass image.",
      "  %",
      "  % Parameters:",
      "  %   class: 0-PseudoClass or 1-Grayscale.",
      "  %",
      "  currentfile buffer readline pop",
      "  token pop /class exch def pop",
      "  class 0 gt",
      "  {",
      "    currentfile buffer readline pop",
      "    token pop /depth exch def pop",
      "    /grays columns 8 add depth sub depth mul 8 idiv string def",
      "    columns rows depth",
      "    [",
      "      columns 0 0",
      "      rows neg 0 rows",
      "    ]",
      "    { currentfile grays readhexstring pop } image",
      "  }",
      "  {",
      "    %",
      "    % Parameters:",
      "    %   colors: number of colors in the colormap.",
      "    %   colormap: red, green, blue color packets.",
      "    %",
      "    currentfile buffer readline pop",
      "    token pop /colors exch def pop",
      "    /colors colors 3 mul def",
      "    /colormap colors string def",
      "    currentfile colormap readhexstring pop pop",
      "    systemdict /colorimage known",
      "    {",
      "      columns rows 8",
      "      [",
      "        columns 0 0",
      "        rows neg 0 rows",
      "      ]",
      "      { PseudoClassPacket } false 3 colorimage",
      "    }",
      "    {",
      "      %",
      "      % No colorimage operator;  convert to grayscale.",
      "      %",
      "      columns rows 8",
      "      [",
      "        columns 0 0",
      "        rows neg 0 rows",
      "      ]",
      "      { GrayPseudoClassPacket } image",
      "    } ifelse",
      "  } ifelse",
      "} bind def",
      "",
      "/DisplayImage",
      "{",
      "  %",
      "  % Display a DirectClass or PseudoClass image.",
      "  %",
      "  % Parameters:",
      "  %   x & y translation.",
      "  %   x & y scale.",
      "  %   label pointsize.",
      "  %   image label.",
      "  %   image columns & rows.",
      "  %   class: 0-DirectClass or 1-PseudoClass.",
      "  %   compression: 0-none or 1-RunlengthEncoded.",
      "  %   hex color packets.",
      "  %",
      "  gsave",
      "  /buffer 512 string def",
      "  /byte 1 string def",
      "  /color_packet 3 string def",
      "  /pixels 768 string def",
      "",
      "  currentfile buffer readline pop",
      "  token pop /x exch def",
      "  token pop /y exch def pop",
      "  x y translate",
      "  currentfile buffer readline pop",
      "  token pop /x exch def",
      "  token pop /y exch def pop",
      "  currentfile buffer readline pop",
      "  token pop /pointsize exch def pop",
      "  /Times-Roman findfont pointsize scalefont setfont",
      (const char *) NULL
    },
    *const PostscriptEpilog[]=
    {
      "  x y scale",
      "  currentfile buffer readline pop",
      "  token pop /columns exch def",
      "  token pop /rows exch def pop",
      "  currentfile buffer readline pop",
      "  token pop /class exch def pop",
      "  currentfile buffer readline pop",
      "  token pop /compression exch def pop",
      "  class 0 gt { PseudoClassImage } { DirectClassImage } ifelse",
      "  grestore",
      (const char *) NULL
    };

  char
    buffer[MagickPathExtent],
    date[MagickPathExtent],
    **labels,
    page_geometry[MagickPathExtent];

  CompressionType
    compression;

  const char
    *const *s,
    *value;

  const StringInfo
    *profile;

  double
    pointsize;

  GeometryInfo
    geometry_info;

  MagickBooleanType
    status;

  MagickOffsetType
    scene;

  MagickStatusType
    flags;

  PixelInfo
    pixel;

  PointInfo
    delta,
    resolution,
    scale;

  Quantum
    index;

  RectangleInfo
    geometry,
    media_info,
    page_info;

  register const Quantum
    *p;

  register ssize_t
    i,
    x;

  register unsigned char
    *q;

  SegmentInfo
    bounds;

  size_t
    bit,
    byte,
    length,
    page,
    text_size;

  ssize_t
    j,
    y;

  time_t
    timer;

  unsigned char
    pixels[2048];

  /*
    Open output image file.
  */
  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickCoreSignature);
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  status=OpenBlob(image_info,image,WriteBinaryBlobMode,exception);
  if (status == MagickFalse)
    return(status);
  (void) ResetMagickMemory(&bounds,0,sizeof(bounds));
  compression=image->compression;
  if (image_info->compression != UndefinedCompression)
    compression=image_info->compression;
  page=1;
  scene=0;
  do
  {
    /*
      Scale relative to dots-per-inch.
    */
    (void) TransformImageColorspace(image,sRGBColorspace,exception);
    delta.x=DefaultResolution;
    delta.y=DefaultResolution;
    resolution.x=image->resolution.x;
    resolution.y=image->resolution.y;
    if ((resolution.x == 0.0) || (resolution.y == 0.0))
      {
        flags=ParseGeometry(PSDensityGeometry,&geometry_info);
        resolution.x=geometry_info.rho;
        resolution.y=geometry_info.sigma;
        if ((flags & SigmaValue) == 0)
          resolution.y=resolution.x;
      }
    if (image_info->density != (char *) NULL)
      {
        flags=ParseGeometry(image_info->density,&geometry_info);
        resolution.x=geometry_info.rho;
        resolution.y=geometry_info.sigma;
        if ((flags & SigmaValue) == 0)
          resolution.y=resolution.x;
      }
    if (image->units == PixelsPerCentimeterResolution)
      {
        resolution.x=(double) ((size_t) (100.0*2.54*resolution.x+0.5)/100.0);
        resolution.y=(double) ((size_t) (100.0*2.54*resolution.y+0.5)/100.0);
      }
    SetGeometry(image,&geometry);
    (void) FormatLocaleString(page_geometry,MagickPathExtent,"%.20gx%.20g",
      (double) image->columns,(double) image->rows);
    if (image_info->page != (char *) NULL)
      (void) CopyMagickString(page_geometry,image_info->page,MagickPathExtent);
    else
      if ((image->page.width != 0) && (image->page.height != 0))
        (void) FormatLocaleString(page_geometry,MagickPathExtent,
          "%.20gx%.20g%+.20g%+.20g",(double) image->page.width,(double)
          image->page.height,(double) image->page.x,(double) image->page.y);
      else
        if ((image->gravity != UndefinedGravity) &&
            (LocaleCompare(image_info->magick,"PS") == 0))
          (void) CopyMagickString(page_geometry,PSPageGeometry,MagickPathExtent);
    (void) ConcatenateMagickString(page_geometry,">",MagickPathExtent);
    (void) ParseMetaGeometry(page_geometry,&geometry.x,&geometry.y,
      &geometry.width,&geometry.height);
    scale.x=(double) (geometry.width*delta.x)/resolution.x;
    geometry.width=(size_t) floor(scale.x+0.5);
    scale.y=(double) (geometry.height*delta.y)/resolution.y;
    geometry.height=(size_t) floor(scale.y+0.5);
    (void) ParseAbsoluteGeometry(page_geometry,&media_info);
    (void) ParseGravityGeometry(image,page_geometry,&page_info,exception);
    if (image->gravity != UndefinedGravity)
      {
        geometry.x=(-page_info.x);
        geometry.y=(ssize_t) (media_info.height+page_info.y-image->rows);
      }
    pointsize=12.0;
    if (image_info->pointsize != 0.0)
      pointsize=image_info->pointsize;
    text_size=0;
    value=GetImageProperty(image,"label",exception);
    if (value != (const char *) NULL)
      text_size=(size_t) (MultilineCensus(value)*pointsize+12);
    if (page == 1)
      {
        /*
          Output Postscript header.
        */
        if (LocaleCompare(image_info->magick,"PS") == 0)
          (void) CopyMagickString(buffer,"%!PS-Adobe-3.0\n",MagickPathExtent);
        else
          (void) CopyMagickString(buffer,"%!PS-Adobe-3.0 EPSF-3.0\n",
            MagickPathExtent);
        (void) WriteBlobString(image,buffer);
        (void) WriteBlobString(image,"%%Creator: (ImageMagick)\n");
        (void) FormatLocaleString(buffer,MagickPathExtent,"%%%%Title: (%s)\n",
          image->filename);
        (void) WriteBlobString(image,buffer);
        timer=time((time_t *) NULL);
        (void) FormatMagickTime(timer,MagickPathExtent,date);
        (void) FormatLocaleString(buffer,MagickPathExtent,
          "%%%%CreationDate: (%s)\n",date);
        (void) WriteBlobString(image,buffer);
        bounds.x1=(double) geometry.x;
        bounds.y1=(double) geometry.y;
        bounds.x2=(double) geometry.x+scale.x;
        bounds.y2=(double) geometry.y+(geometry.height+text_size);
        if ((image_info->adjoin != MagickFalse) &&
            (GetNextImageInList(image) != (Image *) NULL))
          (void) CopyMagickString(buffer,"%%%%BoundingBox: (atend)\n",
            MagickPathExtent);
        else
          {
            (void) FormatLocaleString(buffer,MagickPathExtent,
              "%%%%BoundingBox: %.20g %.20g %.20g %.20g\n",ceil(bounds.x1-0.5),
              ceil(bounds.y1-0.5),floor(bounds.x2+0.5),floor(bounds.y2+0.5));
            (void) WriteBlobString(image,buffer);
            (void) FormatLocaleString(buffer,MagickPathExtent,
              "%%%%HiResBoundingBox: %g %g %g %g\n",bounds.x1,
              bounds.y1,bounds.x2,bounds.y2);
          }
        (void) WriteBlobString(image,buffer);
        profile=GetImageProfile(image,"8bim");
        if (profile != (StringInfo *) NULL)
          {
            /*
              Embed Photoshop profile.
            */
            (void) FormatLocaleString(buffer,MagickPathExtent,
              "%%BeginPhotoshop: %.20g",(double) GetStringInfoLength(profile));
            (void) WriteBlobString(image,buffer);
            for (i=0; i < (ssize_t) GetStringInfoLength(profile); i++)
            {
              if ((i % 32) == 0)
                (void) WriteBlobString(image,"\n% ");
              (void) FormatLocaleString(buffer,MagickPathExtent,"%02X",
                (unsigned int) (GetStringInfoDatum(profile)[i] & 0xff));
              (void) WriteBlobString(image,buffer);
            }
            (void) WriteBlobString(image,"\n%EndPhotoshop\n");
          }
        profile=GetImageProfile(image,"xmp");
DisableMSCWarning(4127)
        if (0 && (profile != (StringInfo *) NULL))
RestoreMSCWarning
          {
            /*
              Embed XML profile.
            */
            (void) WriteBlobString(image,"\n%begin_xml_code\n");
            (void) FormatLocaleString(buffer,MagickPathExtent,
               "\n%%begin_xml_packet: %.20g\n",(double)
               GetStringInfoLength(profile));
            (void) WriteBlobString(image,buffer);
            for (i=0; i < (ssize_t) GetStringInfoLength(profile); i++)
              (void) WriteBlobByte(image,GetStringInfoDatum(profile)[i]);
            (void) WriteBlobString(image,"\n%end_xml_packet\n%end_xml_code\n");
          }
        value=GetImageProperty(image,"label",exception);
        if (value != (const char *) NULL)
          (void) WriteBlobString(image,
            "%%DocumentNeededResources: font Times-Roman\n");
        (void) WriteBlobString(image,"%%DocumentData: Clean7Bit\n");
        (void) WriteBlobString(image,"%%LanguageLevel: 1\n");
        if (LocaleCompare(image_info->magick,"PS") != 0)
          (void) WriteBlobString(image,"%%Pages: 1\n");
        else
          {
            /*
              Compute the number of pages.
            */
            (void) WriteBlobString(image,"%%Orientation: Portrait\n");
            (void) WriteBlobString(image,"%%PageOrder: Ascend\n");
            (void) FormatLocaleString(buffer,MagickPathExtent,"%%%%Pages: %.20g\n",
              image_info->adjoin != MagickFalse ? (double)
              GetImageListLength(image) : 1.0);
            (void) WriteBlobString(image,buffer);
          }
        (void) WriteBlobString(image,"%%EndComments\n");
        (void) WriteBlobString(image,"\n%%BeginDefaults\n");
        (void) WriteBlobString(image,"%%EndDefaults\n\n");
        if ((LocaleCompare(image_info->magick,"EPI") == 0) ||
            (LocaleCompare(image_info->magick,"EPSI") == 0) ||
            (LocaleCompare(image_info->magick,"EPT") == 0))
          {
            Image
              *preview_image;

            Quantum
              pixel;

            register ssize_t
              x;

            ssize_t
              y;

            /*
              Create preview image.
            */
            preview_image=CloneImage(image,0,0,MagickTrue,exception);
            if (preview_image == (Image *) NULL)
              ThrowWriterException(ResourceLimitError,"MemoryAllocationFailed");
            /*
              Dump image as bitmap.
            */
            (void) FormatLocaleString(buffer,MagickPathExtent,
              "%%%%BeginPreview: %.20g %.20g %.20g %.20g\n%%  ",(double)
              preview_image->columns,(double) preview_image->rows,1.0,
              (double) ((((preview_image->columns+7) >> 3)*preview_image->rows+
              35)/36));
            (void) WriteBlobString(image,buffer);
            q=pixels;
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(preview_image,0,y,preview_image->columns,1,
                exception);
              if (p == (const Quantum *) NULL)
                break;
              bit=0;
              byte=0;
              for (x=0; x < (ssize_t) preview_image->columns; x++)
              {
                byte<<=1;
                pixel=ClampToQuantum(GetPixelLuma(preview_image,p));
                if (pixel >= (Quantum) (QuantumRange/2))
                  byte|=0x01;
                bit++;
                if (bit == 8)
                  {
                    q=PopHexPixel(hex_digits,byte,q);
                    if ((q-pixels+8) >= 80)
                      {
                        *q++='\n';
                        (void) WriteBlob(image,q-pixels,pixels);
                        q=pixels;
                        (void) WriteBlobString(image,"%  ");
                      };
                    bit=0;
                    byte=0;
                  }
              }
              if (bit != 0)
                {
                  byte<<=(8-bit);
                  q=PopHexPixel(hex_digits,byte,q);
                  if ((q-pixels+8) >= 80)
                    {
                      *q++='\n';
                      (void) WriteBlob(image,q-pixels,pixels);
                      q=pixels;
                      (void) WriteBlobString(image,"%  ");
                    };
                };
            }
            if (q != pixels)
              {
                *q++='\n';
                (void) WriteBlob(image,q-pixels,pixels);
              }
            (void) WriteBlobString(image,"\n%%EndPreview\n");
            preview_image=DestroyImage(preview_image);
          }
        /*
          Output Postscript commands.
        */
        for (s=PostscriptProlog; *s != (char *) NULL; s++)
        {
          (void) FormatLocaleString(buffer,MagickPathExtent,"%s\n",*s);
          (void) WriteBlobString(image,buffer);
        }
        value=GetImageProperty(image,"label",exception);
        if (value != (const char *) NULL)
          for (j=(ssize_t) MultilineCensus(value)-1; j >= 0; j--)
          {
            (void) WriteBlobString(image,"  /label 512 string def\n");
            (void) WriteBlobString(image,"  currentfile label readline pop\n");
            (void) FormatLocaleString(buffer,MagickPathExtent,
              "  0 y %g add moveto label show pop\n",j*pointsize+12);
            (void) WriteBlobString(image,buffer);
          }
        for (s=PostscriptEpilog; *s != (char *) NULL; s++)
        {
          (void) FormatLocaleString(buffer,MagickPathExtent,"%s\n",*s);
          (void) WriteBlobString(image,buffer);
        }
        if (LocaleCompare(image_info->magick,"PS") == 0)
          (void) WriteBlobString(image,"  showpage\n");
        (void) WriteBlobString(image,"} bind def\n");
        (void) WriteBlobString(image,"%%EndProlog\n");
      }
    (void) FormatLocaleString(buffer,MagickPathExtent,"%%%%Page:  1 %.20g\n",
      (double) (page++));
    (void) WriteBlobString(image,buffer);
    (void) FormatLocaleString(buffer,MagickPathExtent,
      "%%%%PageBoundingBox: %.20g %.20g %.20g %.20g\n",(double) geometry.x,
      (double) geometry.y,geometry.x+(double) geometry.width,geometry.y+(double)
      (geometry.height+text_size));
    (void) WriteBlobString(image,buffer);
    if ((double) geometry.x < bounds.x1)
      bounds.x1=(double) geometry.x;
    if ((double) geometry.y < bounds.y1)
      bounds.y1=(double) geometry.y;
    if ((double) (geometry.x+geometry.width-1) > bounds.x2)
      bounds.x2=(double) geometry.x+geometry.width-1;
    if ((double) (geometry.y+(geometry.height+text_size)-1) > bounds.y2)
      bounds.y2=(double) geometry.y+(geometry.height+text_size)-1;
    value=GetImageProperty(image,"label",exception);
    if (value != (const char *) NULL)
      (void) WriteBlobString(image,"%%%%PageResources: font Times-Roman\n");
    if (LocaleCompare(image_info->magick,"PS") != 0)
      (void) WriteBlobString(image,"userdict begin\n");
    (void) WriteBlobString(image,"DisplayImage\n");
    /*
      Output image data.
    */
    (void) FormatLocaleString(buffer,MagickPathExtent,"%.20g %.20g\n%g %g\n%g\n",
      (double) geometry.x,(double) geometry.y,scale.x,scale.y,pointsize);
    (void) WriteBlobString(image,buffer);
    labels=(char **) NULL;
    value=GetImageProperty(image,"label",exception);
    if (value != (const char *) NULL)
      labels=StringToList(value);
    if (labels != (char **) NULL)
      {
        for (i=0; labels[i] != (char *) NULL; i++)
        {
          (void) FormatLocaleString(buffer,MagickPathExtent,"%s \n",
            labels[i]);
          (void) WriteBlobString(image,buffer);
          labels[i]=DestroyString(labels[i]);
        }
        labels=(char **) RelinquishMagickMemory(labels);
      }
    (void) ResetMagickMemory(&pixel,0,sizeof(pixel));
    pixel.alpha=(MagickRealType) TransparentAlpha;
    index=0;
    x=0;
    if ((image_info->type != TrueColorType) &&
        (SetImageGray(image,exception) != MagickFalse))
      {
        if (SetImageMonochrome(image,exception) == MagickFalse)
          {
            Quantum
              pixel;

            /*
              Dump image as grayscale.
            */
            (void) FormatLocaleString(buffer,MagickPathExtent,
              "%.20g %.20g\n1\n1\n1\n8\n",(double) image->columns,(double)
              image->rows);
            (void) WriteBlobString(image,buffer);
            q=pixels;
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,exception);
              if (p == (const Quantum *) NULL)
                break;
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                pixel=(Quantum) ScaleQuantumToChar(ClampToQuantum(GetPixelLuma(
                  image,p)));
                q=PopHexPixel(hex_digits,(size_t) pixel,q);
                if ((q-pixels+8) >= 80)
                  {
                    *q++='\n';
                    (void) WriteBlob(image,q-pixels,pixels);
                    q=pixels;
                  }
                p+=GetPixelChannels(image);
              }
              if (image->previous == (Image *) NULL)
                {
                  status=SetImageProgress(image,SaveImageTag,(MagickOffsetType)
                    y,image->rows);
                  if (status == MagickFalse)
                    break;
                }
            }
            if (q != pixels)
              {
                *q++='\n';
                (void) WriteBlob(image,q-pixels,pixels);
              }
          }
        else
          {
            ssize_t
              y;

            Quantum
              pixel;

            /*
              Dump image as bitmap.
            */
            (void) FormatLocaleString(buffer,MagickPathExtent,
              "%.20g %.20g\n1\n1\n1\n1\n",(double) image->columns,(double)
              image->rows);
            (void) WriteBlobString(image,buffer);
            q=pixels;
            for (y=0; y < (ssize_t) image->rows; y++)
            {
              p=GetVirtualPixels(image,0,y,image->columns,1,exception);
              if (p == (const Quantum *) NULL)
                break;
              bit=0;
              byte=0;
              for (x=0; x < (ssize_t) image->columns; x++)
              {
                byte<<=1;
                pixel=ClampToQuantum(GetPixelLuma(image,p));
                if (pixel >= (Quantum) (QuantumRange/2))
                  byte|=0x01;
                bit++;
                if (bit == 8)
                  {
                    q=PopHexPixel(hex_digits,byte,q);
                    if ((q-pixels+2) >= 80)
                      {
                        *q++='\n';
                        (void) WriteBlob(image,q-pixels,pixels);
                        q=pixels;
                      };
                    bit=0;
                    byte=0;
                  }
                p+=GetPixelChannels(image);
              }
              if (bit != 0)
                {
                  byte<<=(8-bit);
                  q=PopHexPixel(hex_digits,byte,q);
                  if ((q-pixels+2) >= 80)
                    {
                      *q++='\n';
                      (void) WriteBlob(image,q-pixels,pixels);
                      q=pixels;
                    }
                };
              if (image->previous == (Image *) NULL)
                {
                  status=SetImageProgress(image,SaveImageTag,(MagickOffsetType)
                    y,image->rows);
                  if (status == MagickFalse)
                    break;
                }
            }
            if (q != pixels)
              {
                *q++='\n';
                (void) WriteBlob(image,q-pixels,pixels);
              }
          }
      }
    else
      if ((image->storage_class == DirectClass) ||
          (image->colors > 256) || (image->alpha_trait != UndefinedPixelTrait))
        {
          /*
            Dump DirectClass image.
          */
          (void) FormatLocaleString(buffer,MagickPathExtent,"%.20g %.20g\n0\n%d\n",
            (double) image->columns,(double) image->rows,
            compression == RLECompression ? 1 : 0);
          (void) WriteBlobString(image,buffer);
          switch (compression)
          {
            case RLECompression:
            {
              /*
                Dump runlength-encoded DirectColor packets.
              */
              q=pixels;
              for (y=0; y < (ssize_t) image->rows; y++)
              {
                p=GetVirtualPixels(image,0,y,image->columns,1,exception);
                if (p == (const Quantum *) NULL)
                  break;
                GetPixelInfoPixel(image,p,&pixel);
                length=255;
                for (x=0; x < (ssize_t) image->columns; x++)
                {
                  if ((GetPixelRed(image,p) == ClampToQuantum(pixel.red)) &&
                      (GetPixelGreen(image,p) == ClampToQuantum(pixel.green)) &&
                      (GetPixelBlue(image,p) == ClampToQuantum(pixel.blue)) &&
                      (GetPixelAlpha(image,p) == ClampToQuantum(pixel.alpha)) &&
                      (length < 255) && (x < (ssize_t) (image->columns-1)))
                    length++;
                  else
                    {
                      if (x > 0)
                        {
                          WriteRunlengthPacket(image,pixel,length,p);
                          if ((q-pixels+10) >= 80)
                            {
                              *q++='\n';
                              (void) WriteBlob(image,q-pixels,pixels);
                              q=pixels;
                            }
                        }
                      length=0;
                    }
                  GetPixelInfoPixel(image,p,&pixel);
                  p+=GetPixelChannels(image);
                }
                WriteRunlengthPacket(image,pixel,length,p);
                if ((q-pixels+10) >= 80)
                  {
                    *q++='\n';
                    (void) WriteBlob(image,q-pixels,pixels);
                    q=pixels;
                  }
                if (image->previous == (Image *) NULL)
                  {
                    status=SetImageProgress(image,SaveImageTag,
                      (MagickOffsetType) y,image->rows);
                    if (status == MagickFalse)
                      break;
                  }
              }
              if (q != pixels)
                {
                  *q++='\n';
                  (void) WriteBlob(image,q-pixels,pixels);
                }
              break;
            }
            case NoCompression:
            default:
            {
              /*
                Dump uncompressed DirectColor packets.
              */
              q=pixels;
              for (y=0; y < (ssize_t) image->rows; y++)
              {
                p=GetVirtualPixels(image,0,y,image->columns,1,exception);
                if (p == (const Quantum *) NULL)
                  break;
                for (x=0; x < (ssize_t) image->columns; x++)
                {
                  if ((image->alpha_trait != UndefinedPixelTrait) &&
                      (GetPixelAlpha(image,p) == (Quantum) TransparentAlpha))
                    {
                      q=PopHexPixel(hex_digits,0xff,q);
                      q=PopHexPixel(hex_digits,0xff,q);
                      q=PopHexPixel(hex_digits,0xff,q);
                    }
                  else
                    {
                      q=PopHexPixel(hex_digits,ScaleQuantumToChar(
                        GetPixelRed(image,p)),q);
                      q=PopHexPixel(hex_digits,ScaleQuantumToChar(
                        GetPixelGreen(image,p)),q);
                      q=PopHexPixel(hex_digits,ScaleQuantumToChar(
                        GetPixelBlue(image,p)),q);
                    }
                  if ((q-pixels+6) >= 80)
                    {
                      *q++='\n';
                      (void) WriteBlob(image,q-pixels,pixels);
                      q=pixels;
                    }
                  p+=GetPixelChannels(image);
                }
                if (image->previous == (Image *) NULL)
                  {
                    status=SetImageProgress(image,SaveImageTag,
                      (MagickOffsetType) y,image->rows);
                    if (status == MagickFalse)
                      break;
                  }
              }
              if (q != pixels)
                {
                  *q++='\n';
                  (void) WriteBlob(image,q-pixels,pixels);
                }
              break;
            }
          }
          (void) WriteBlobByte(image,'\n');
        }
      else
        {
          /*
            Dump PseudoClass image.
          */
          (void) FormatLocaleString(buffer,MagickPathExtent,
            "%.20g %.20g\n%d\n%d\n0\n",(double) image->columns,(double)
            image->rows,image->storage_class == PseudoClass ? 1 : 0,
            compression == RLECompression ? 1 : 0);
          (void) WriteBlobString(image,buffer);
          /*
            Dump number of colors and colormap.
          */
          (void) FormatLocaleString(buffer,MagickPathExtent,"%.20g\n",(double)
            image->colors);
          (void) WriteBlobString(image,buffer);
          for (i=0; i < (ssize_t) image->colors; i++)
          {
            (void) FormatLocaleString(buffer,MagickPathExtent,"%02X%02X%02X\n",
              ScaleQuantumToChar(ClampToQuantum(image->colormap[i].red)),
              ScaleQuantumToChar(ClampToQuantum(image->colormap[i].green)),
              ScaleQuantumToChar(ClampToQuantum(image->colormap[i].blue)));
            (void) WriteBlobString(image,buffer);
          }
          switch (compression)
          {
            case RLECompression:
            {
              /*
                Dump runlength-encoded PseudoColor packets.
              */
              q=pixels;
              for (y=0; y < (ssize_t) image->rows; y++)
              {
                p=GetVirtualPixels(image,0,y,image->columns,1,exception);
                if (p == (const Quantum *) NULL)
                  break;
                index=GetPixelIndex(image,p);
                length=255;
                for (x=0; x < (ssize_t) image->columns; x++)
                {
                  if ((index == GetPixelIndex(image,p)) &&
                      (length < 255) && (x < ((ssize_t) image->columns-1)))
                    length++;
                  else
                    {
                      if (x > 0)
                        {
                          q=PopHexPixel(hex_digits,(size_t) index,q);
                          q=PopHexPixel(hex_digits,(size_t)
                            MagickMin(length,0xff),q);
                          i++;
                          if ((q-pixels+6) >= 80)
                            {
                              *q++='\n';
                              (void) WriteBlob(image,q-pixels,pixels);
                              q=pixels;
                            }
                        }
                      length=0;
                    }
                  index=GetPixelIndex(image,p);
                  pixel.red=(MagickRealType) GetPixelRed(image,p);
                  pixel.green=(MagickRealType) GetPixelGreen(image,p);
                  pixel.blue=(MagickRealType) GetPixelBlue(image,p);
                  pixel.alpha=(MagickRealType) GetPixelAlpha(image,p);
                  p+=GetPixelChannels(image);
                }
                q=PopHexPixel(hex_digits,(size_t) index,q);
                q=PopHexPixel(hex_digits,(size_t)
                  MagickMin(length,0xff),q);
                if (image->previous == (Image *) NULL)
                  {
                    status=SetImageProgress(image,SaveImageTag,
                      (MagickOffsetType) y,image->rows);
                    if (status == MagickFalse)
                      break;
                  }
              }
              if (q != pixels)
                {
                  *q++='\n';
                  (void) WriteBlob(image,q-pixels,pixels);
                }
              break;
            }
            case NoCompression:
            default:
            {
              /*
                Dump uncompressed PseudoColor packets.
              */
              q=pixels;
              for (y=0; y < (ssize_t) image->rows; y++)
              {
                p=GetVirtualPixels(image,0,y,image->columns,1,exception);
                if (p == (const Quantum *) NULL)
                  break;
                for (x=0; x < (ssize_t) image->columns; x++)
                {
                  q=PopHexPixel(hex_digits,(size_t) GetPixelIndex(image,p),q);
                  if ((q-pixels+4) >= 80)
                    {
                      *q++='\n';
                      (void) WriteBlob(image,q-pixels,pixels);
                      q=pixels;
                    }
                  p+=GetPixelChannels(image);
                }
                if (image->previous == (Image *) NULL)
                  {
                    status=SetImageProgress(image,SaveImageTag,
                      (MagickOffsetType) y,image->rows);
                    if (status == MagickFalse)
                      break;
                  }
              }
              if (q != pixels)
                {
                  *q++='\n';
                  (void) WriteBlob(image,q-pixels,pixels);
                }
              break;
            }
          }
          (void) WriteBlobByte(image,'\n');
        }
    if (LocaleCompare(image_info->magick,"PS") != 0)
      (void) WriteBlobString(image,"end\n");
    (void) WriteBlobString(image,"%%PageTrailer\n");
    if (GetNextImageInList(image) == (Image *) NULL)
      break;
    image=SyncNextImageInList(image);
    status=SetImageProgress(image,SaveImagesTag,scene++,
      GetImageListLength(image));
    if (status == MagickFalse)
      break;
  } while (image_info->adjoin != MagickFalse);
  (void) WriteBlobString(image,"%%Trailer\n");
  if (page > 2)
    {
      (void) FormatLocaleString(buffer,MagickPathExtent,
        "%%%%BoundingBox: %.20g %.20g %.20g %.20g\n",ceil(bounds.x1-0.5),
        ceil(bounds.y1-0.5),floor(bounds.x2-0.5),floor(bounds.y2-0.5));
      (void) WriteBlobString(image,buffer);
      (void) FormatLocaleString(buffer,MagickPathExtent,
        "%%%%HiResBoundingBox: %g %g %g %g\n",bounds.x1,bounds.y1,bounds.x2,
        bounds.y2);
      (void) WriteBlobString(image,buffer);
    }
  (void) WriteBlobString(image,"%%EOF\n");
  (void) CloseBlob(image);
  return(MagickTrue);
}
