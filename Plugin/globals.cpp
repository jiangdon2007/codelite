//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2008 by Eran Ifrah
// file name            : globals.cpp
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
#include "precompiled_header.h"
#include <wx/wfstream.h>
#include <wx/log.h>
#include <wx/imaglist.h>
#include <wx/xrc/xmlres.h>
#include "dirtraverser.h"
#include "imanager.h"
#include "environmentconfig.h"
#include <wx/dataobj.h>
#include <wx/stdpaths.h>
#include "drawingutils.h"
#include <wx/dir.h>
#include "editor_config.h"
#include "workspace.h"
#include "project.h"
#include "wx/tokenzr.h"
#include "globals.h"
#include "wx/app.h"
#include "wx/window.h"
#include "wx/listctrl.h"
#include "wx/ffile.h"
#include "procutils.h"
#include <wx/clipbrd.h>
#include "ieditor.h"
#include <wx/tokenzr.h>
#include <set>
#include <wx/fontmap.h>
#include <wx/zipstrm.h>
#include <wx/filename.h>

#ifdef __WXMSW__
#include <Uxtheme.h>
#endif

static wxString DoExpandAllVariables(const wxString &expression, Workspace *workspace, const wxString &projectName, const wxString &confToBuild, const wxString &fileName);

#ifdef __WXMAC__
#include <mach-o/dyld.h>

//On Mac we determine the base path using system call
//_NSGetExecutablePath(path, &path_len);
static wxString MacGetInstallPath()
{
	char path[257];
	uint32_t path_len = 256;
	_NSGetExecutablePath(path, &path_len);

	//path now contains
	//CodeLite.app/Contents/MacOS/
	wxFileName fname(wxString(path, wxConvUTF8));

	//remove he MacOS part of the exe path
	wxString file_name = fname.GetPath(wxPATH_GET_VOLUME|wxPATH_GET_SEPARATOR);
	wxString rest;
	file_name.EndsWith(wxT("MacOS/"), &rest);
	rest.Append(wxT("SharedSupport/"));

	return rest;
}
#endif

#if defined(__WXGTK__)
    #include <unistd.h>
    #include <dirent.h>
#endif

static bool IsBOMFile(const char* file_name)
{
	bool res (false);
	FILE *fp = fopen(file_name, "rb");
	if ( fp ) {
		struct stat buff;
		if ( stat(file_name, &buff) == 0 ) {
			
			// Read the first 4 bytes (or less)
			size_t size = buff.st_size;
			if(size > 4) size = 4;
			
			char *buffer = new char[size];
			if ( fread(buffer, sizeof(char), size, fp) == size ) {
				BOM bom(buffer, size);
				res = (bom.Encoding() != wxFONTENCODING_SYSTEM);
			}
			delete [] buffer;
		}
		fclose(fp);
	}
	return res;
}

static bool ReadBOMFile(const char *file_name, wxString &content, BOM& bom)
{
	content.Empty();

	FILE *fp = fopen(file_name, "rb");
	if ( fp ) {
		struct stat buff;
		if ( stat(file_name, &buff) == 0 ) {
			size_t size = buff.st_size;
			char *buffer = new char[size+1];
			if ( fread(buffer, sizeof(char), size, fp) == size ) {
				buffer[size] = 0;
				
				wxFontEncoding encoding(wxFONTENCODING_SYSTEM);
				size_t         bomSize (size);
				
				if(bomSize > 4) bomSize = 4;
				bom.SetData(buffer, bomSize);
				encoding = bom.Encoding();
				
				if(encoding != wxFONTENCODING_SYSTEM) {
					wxCSConv conv(encoding);
					// Skip the BOM
					content = wxString(buffer, conv);
				}
			}
			delete [] buffer;
		}
		fclose(fp);
	} // From8BitData
	return content.IsEmpty() == false;
}

static bool ReadFile8BitData(const char *file_name, wxString &content)
{
	content.Empty();

	FILE *fp = fopen(file_name, "rb");
	if ( fp ) {
		struct stat buff;
		if ( stat(file_name, &buff) == 0 ) {
			size_t size = buff.st_size;
			char *buffer = new char[size+1];
			if ( fread(buffer, sizeof(char), size, fp) == size ) {
				buffer[size] = 0;
				content = wxString::From8BitData(buffer);
			}
			delete [] buffer;
		}
		fclose(fp);
	}
	return content.IsEmpty() == false;
}

bool SendCmdEvent(int eventId, void *clientData)
{
	wxCommandEvent e(eventId);
	if (clientData) {
		e.SetClientData(clientData);
	}
	return wxTheApp->ProcessEvent(e);
}

bool SendCmdEvent(int eventId, void *clientData, const wxString &str)
{
	wxCommandEvent e(eventId);
	e.SetClientData(clientData);
	e.SetString(str);
	return wxTheApp->ProcessEvent(e);
}

void PostCmdEvent(int eventId, void *clientData)
{
	wxCommandEvent e(eventId);
	if (clientData) {
		e.SetClientData(clientData);
	}
	wxTheApp->AddPendingEvent(e);
}

void SetColumnText (wxListCtrl *list, long indx, long column, const wxString &rText, int imgId )
{
	wxListItem list_item;
	list_item.SetId ( indx );
	list_item.SetColumn ( column );
	list_item.SetMask ( wxLIST_MASK_TEXT );
	list_item.SetText ( rText );
	list_item.SetImage( imgId );
	list->SetItem ( list_item );
}

wxString GetColumnText(wxListCtrl *list, long index, long column)
{
	wxListItem list_item;
	list_item.SetId ( index );
	list_item.SetColumn ( column );
	list_item.SetMask ( wxLIST_MASK_TEXT );
	list->GetItem ( list_item );
	return list_item.GetText();
}

bool ReadFileWithConversion(const wxString &fileName, wxString &content, wxFontEncoding encoding, BOM *bom)
{
	wxLogNull noLog;
	content.Clear();
	wxFFile file(fileName, wxT("rb"));
	
	const wxCharBuffer name = _C(fileName);
	if (file.IsOpened()) {
		
		// If we got a BOM pointer, test to see whether the file is BOM file
		if(bom && IsBOMFile(name.data())) {
			return ReadBOMFile(name.data(), content, *bom);
		}
		
		if (encoding == wxFONTENCODING_DEFAULT)
			encoding = EditorConfigST::Get()->GetOptions()->GetFileFontEncoding();
			
		// first try the user defined encoding (except for UTF8: the UTF8 builtin appears to be faster)
		if (encoding != wxFONTENCODING_UTF8) {
			wxCSConv fontEncConv(encoding);
			if (fontEncConv.IsOk()) {
				file.ReadAll(&content, fontEncConv);
			}
		}
		
		if (content.IsEmpty()) {
			// now try the Utf8
			file.ReadAll(&content, wxConvUTF8);
			if (content.IsEmpty()) {
				// try local 8 bit data
				ReadFile8BitData(name.data(), content);
			}
		}
	}
	return !content.IsEmpty();
}

bool RemoveDirectory(const wxString &path)
{
	wxString cmd;
	if (wxGetOsVersion() & wxOS_WINDOWS) {
		//any of the windows variants
		cmd << wxT("rmdir /S /Q ") << wxT("\"") << path << wxT("\"");
	} else {
		cmd << wxT("\rm -fr ") << wxT("\"") << path << wxT("\"");
	}
	return wxShell(cmd);
}

bool IsValidCppIndetifier(const wxString &id)
{
	if (id.IsEmpty()) {
		return false;
	}
	//first char can be only _A-Za-z
	wxString first( id.Mid(0, 1) );
	if (first.find_first_not_of(wxT("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")) != wxString::npos ) {
		return false;
	}
	//make sure that rest of the id contains only a-zA-Z0-9_
	if (id.find_first_not_of(wxT("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")) != wxString::npos) {
		return false;
	}
	return true;
}

long AppendListCtrlRow(wxListCtrl *list)
{
	long item;
	list->GetItemCount() ? item = list->GetItemCount() : item = 0;

	wxListItem info;
	// Set the item display name
	info.SetColumn(0);
	info.SetId(item);
	item = list->InsertItem(info);
	return item;
}

bool IsValidCppFile(const wxString &id)
{
	if (id.IsEmpty()) {
		return false;
	}

	//make sure that rest of the id contains only a-zA-Z0-9_
	if (id.find_first_not_of(wxT("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")) != wxString::npos) {
		return false;
	}
	return true;
}

wxString ExpandVariables(const wxString &expression, ProjectPtr proj, IEditor *editor)
{
	wxString project_name ( proj->GetName() );
	wxString fileName;
	if ( editor ) {
		fileName = editor->GetFileName().GetFullPath();
	}
	return ExpandAllVariables ( expression, WorkspaceST::Get(), project_name, wxEmptyString, fileName );

}

// This functions accepts expression and expand all variables in it
wxString ExpandAllVariables(const wxString &expression, Workspace *workspace, const wxString &projectName, const wxString &selConf, const wxString &fileName)
{
	//add support for backticks commands
	wxString tmpExp;
	wxString noBackticksExpression;
	for (size_t i=0; i< expression.Length(); i++) {
		if (expression.GetChar(i) == wxT('`')) {
			//found a backtick, loop over until we found the closing backtick
			wxString backtick;
			bool found(false);
			i++;
			for (; i< expression.Length(); i++) {
				if (expression.GetChar(i) == wxT('`')) {
					found = true;
					i++;
					break;
				}
				backtick << expression.GetChar(i);
			}

			if (!found) {
				//dont replace anything
				wxLogMessage(wxT("Syntax error in expression: ") + expression + wxT(": expecting '`'"));
				return expression;
			} else {
				//expand the backtick statement
				wxString expandedBacktick = DoExpandAllVariables(backtick, workspace, projectName, selConf, fileName);

				//execute the backtick
				wxArrayString output;
				ProcUtils::SafeExecuteCommand(expandedBacktick, output);

				//concatenate the array into sAssign To:pace delimited string
				backtick.Clear();
				for (size_t xx=0; xx < output.GetCount(); xx++) {
					backtick << output.Item(xx).Trim().Trim(false) << wxT(" ");
				}

				//and finally concatente the result of the backtick command back to the expression
				tmpExp << backtick;
			}
		} else {
			tmpExp << expression.GetChar(i);
		}
	}

	return DoExpandAllVariables(tmpExp, workspace, projectName, selConf, fileName);
}

wxString DoExpandAllVariables(const wxString &expression, Workspace *workspace, const wxString &projectName, const wxString &confToBuild, const wxString &fileName)
{
	wxString errMsg;
	wxString output(expression);
	if ( workspace ) {
		output.Replace(wxT("$(WorkspaceName)"), workspace->GetName());
		ProjectPtr proj = workspace->FindProjectByName(projectName, errMsg);
		if (proj) {
			wxString project_name(proj->GetName());

			//make sure that the project name does not contain any spaces
			project_name.Replace(wxT(" "), wxT("_"));

			BuildConfigPtr bldConf = workspace->GetProjBuildConf(proj->GetName(), confToBuild);
			output.Replace(wxT("$(ProjectPath)"),   proj->GetFileName().GetPath(wxPATH_GET_VOLUME|wxPATH_GET_SEPARATOR));
			output.Replace(wxT("$(WorkspacePath)"), workspace->GetWorkspaceFileName().GetPath(wxPATH_GET_VOLUME|wxPATH_GET_SEPARATOR));
			output.Replace(wxT("$(ProjectName)"),   project_name);

			if (bldConf) {
				output.Replace(wxT("$(ConfigurationName)"), bldConf->GetName());

				// the IntermediateDirectory variable is special, since it can contains
				// other variables in it.
				wxString id(bldConf->GetIntermediateDirectory());

				// Substitute all macros from $(IntermediateDirectory)
				id.Replace(wxT("$(ProjectPath)"),       proj->GetFileName().GetPath(wxPATH_GET_VOLUME|wxPATH_GET_SEPARATOR));
				id.Replace(wxT("$(WorkspacePath)"),     workspace->GetWorkspaceFileName().GetPath(wxPATH_GET_VOLUME|wxPATH_GET_SEPARATOR));
				id.Replace(wxT("$(ProjectName)"),       project_name);
				id.Replace(wxT("$(ConfigurationName)"), bldConf->GetName());

				output.Replace(wxT("$(IntermediateDirectory)"), id);
				output.Replace(wxT("$(OutDir)"),                id);
			}

			if(output.Find(wxT("$(ProjectFiles)")) != wxNOT_FOUND)
				output.Replace(wxT("$(ProjectFiles)"),   proj->GetFiles());

			if(output.Find(wxT("$(ProjectFilesAbs)")) != wxNOT_FOUND)
				output.Replace(wxT("$(ProjectFilesAbs)"),proj->GetFiles(true));
		}
	}

	if (fileName.IsEmpty() == false) {
		wxFileName fn(fileName);

		output.Replace(wxT("$(CurrentFileName)"), fn.GetName());

		wxString fpath(fn.GetPath());
		fpath.Replace(wxT("\\"), wxT("/"));
		output.Replace(wxT("$(CurrentFilePath)"), fpath);
		output.Replace(wxT("$(CurrentFileExt)"), fn.GetExt());

		wxString ffullpath(fn.GetFullPath());
		ffullpath.Replace(wxT("\\"), wxT("/"));
		output.Replace(wxT("$(CurrentFileFullPath)"), ffullpath);
	}

	//exapnd common macros
	wxDateTime now = wxDateTime::Now();
	output.Replace(wxT("$(User)"), wxGetUserName());
	output.Replace(wxT("$(Date)"), now.FormatDate());

	if (workspace) {
		output.Replace(wxT("$(CodeLitePath)"), workspace->GetStartupDir());
	}

	//call the environment & workspace variables expand function
	output = EnvironmentConfig::Instance()->ExpandVariables(output, true);
	return output;
}

bool WriteFileUTF8(const wxString& fileName, const wxString& content)
{
	wxFFile file(fileName, wxT("w+b"));

	//first try the Utf8
	return file.Write(content, wxConvUTF8) == content.Length();
}
bool CopyDir(const wxString& src, const wxString& target)
{
	wxString SLASH = wxFileName::GetPathSeparator();

	wxString from(src);
	wxString to(target);

	// append a slash if there is not one (for easier parsing)
	// because who knows what people will pass to the function.
	if (to.EndsWith(SLASH) == false) {
		to << SLASH;
	}

	// for both dirs
	if (from.EndsWith(SLASH) == false) {
		from << SLASH;
	}

	// first make sure that the source dir exists
	if (!wxDir::Exists(from)) {
		Mkdir(from);
		return false;
	}

	if (!wxDir::Exists(to)) {
		Mkdir(to);
	}

	wxDir dir(from);
	wxString filename;
	bool bla = dir.GetFirst(&filename);
	if (bla) {
		do {
			if (wxDirExists(from + filename) ) {
				Mkdir(to + filename);
				CopyDir(from + filename, to + filename);
			} else {
				// change the umask for files only
				wxCopyFile(from + filename, to + filename);
			}
		} while (dir.GetNext(&filename) );
	}
	return true;
}

void Mkdir(const wxString& path)
{
#ifdef __WXMSW__
	wxMkDir(path.GetData());
#else
	wxMkDir(path.ToAscii(), 0777);
#endif
}

bool WriteFileWithBackup(const wxString &file_name, const wxString &content, bool backup)
{
	if (backup) {
		wxString backup_name(file_name);
		backup_name << wxT(".bak");
		if (!wxCopyFile(file_name, backup_name, true)) {
			wxLogMessage(wxString::Format(wxT("Failed to backup file %s, skipping it"), file_name.c_str()));
			return false;
		}
	}

	wxFFile file(file_name, wxT("wb"));
	if (file.IsOpened() == false) {
		// Nothing to be done
		wxString msg = wxString::Format(wxT("Failed to open file %s"), file_name.c_str());
		wxLogMessage( msg );
		return false;
	}

	// write the new content
	wxCSConv fontEncConv(EditorConfigST::Get()->GetOptions()->GetFileFontEncoding());
	file.Write(content, fontEncConv); // JK was without conversion
	file.Close();
	return true;
}

bool CopyToClipboard(const wxString& text)
{
	bool ret(true);

#if wxUSE_CLIPBOARD
	if (wxTheClipboard->Open()) {
		wxTheClipboard->UsePrimarySelection(false);
		if (!wxTheClipboard->SetData(new wxTextDataObject(text))) {
			ret = false;
		}
		wxTheClipboard->Close();
	} else {
		ret = false;
	}
#else // wxUSE_CLIPBOARD
	ret = false;
#endif
	return ret;
}

wxColour MakeColourLighter(wxColour color, float level)
{
	return DrawingUtils::LightColour(color, level);
}

bool IsFileReadOnly(const wxFileName& filename)
{
#ifdef __WXMSW__
	DWORD dwAttrs = GetFileAttributes(filename.GetFullPath().c_str());
	if (dwAttrs != INVALID_FILE_ATTRIBUTES && (dwAttrs & FILE_ATTRIBUTE_READONLY)) {
		return true;
	} else {
		return false;
	}
#else
	// try to open the file with 'write permission'
	return !filename.IsFileWritable();
#endif
}

void FillFromSmiColonString(wxArrayString &arr, const wxString &str)
{
	arr.clear();
	wxStringTokenizer tkz(str, wxT(";"));
	while (tkz.HasMoreTokens()) {

		wxString token = tkz.NextToken();
		token.Trim().Trim(false);
		if (token.IsEmpty()) {
			continue;
		}
		arr.Add(token.Trim());

	}
}

wxString ArrayToSmiColonString(const wxArrayString &array)
{
	wxString result;
	for (size_t i=0; i<array.GetCount(); i++) {
		wxString tmp = NormalizePath(array.Item(i));
		tmp.Trim().Trim(false);
		if ( tmp.IsEmpty() == false ) {
			result += NormalizePath(array.Item(i));
			result += wxT(";");
		}
	}
	return result.BeforeLast(wxT(';'));
}

void StripSemiColons(wxString &str)
{
	str.Replace(wxT(";"), wxT(" "));
}

wxString NormalizePath(const wxString &path)
{
	wxString normalized_path(path);
	normalized_path.Replace(wxT("\\"), wxT("/"));
	return normalized_path;
}

time_t GetFileModificationTime(const wxFileName &filename)
{
	return GetFileModificationTime(filename.GetFullPath());
}

time_t GetFileModificationTime(const wxString &filename)
{
	struct stat buff;
	const wxCharBuffer cname = _C(filename);
	if (stat(cname.data(), &buff) < 0) {
		return 0;
	}
	return buff.st_mtime;
}

void WrapInShell(wxString& cmd)
{
	wxString command;
#ifdef __WXMSW__
    wxChar *shell = wxGetenv(wxT("COMSPEC"));
    if ( !shell )
       shell = (wxChar*) wxT("\\COMMAND.COM");

	command << shell << wxT(" /c \"");
	command << cmd << wxT("\"");
	cmd = command;
#else
	command << wxT("/bin/sh -c '");
	command << cmd << wxT("'");
	cmd = command;
#endif
}


wxString clGetUserName()
{
    wxString squashedname, name = wxGetUserName();

    // The wx doc says that 'name' may now be e.g. "Mr. John Smith"
    // So try to make it more suitable to be an extension
    name.MakeLower();
	name.Replace(wxT(" "), wxT("_"));
	for (size_t i=0; i<name.Len(); ++i) {
		wxChar ch = name.GetChar(i);
		if( (ch < wxT('a') || ch > wxT('z')) && ch != wxT('_')){
			// Non [a-z_] character: skip it
		} else {
			squashedname << ch;
		}
	}

	return (squashedname.IsEmpty() ? wxString(wxT("someone")) : squashedname);
}

void GetProjectTemplateList ( IManager *manager, std::list<ProjectPtr> &list, std::map<wxString,int> *imageMap, wxImageList **lstImages )
{
	wxString tmplateDir = manager->GetStartupDirectory() + wxFileName::GetPathSeparator() + wxT ( "templates/projects" );

	//read all files under this directory
	DirTraverser dt ( wxT ( "*.project" ) );

	wxDir dir ( tmplateDir );
	dir.Traverse ( dt );

	wxArrayString &files = dt.GetFiles();

	if ( files.GetCount() > 0 ) {

		// Allocate image list
		if(imageMap) {
			// add the default icon at position 0
			*lstImages = new wxImageList(24, 24, true);
			//(*lstImages)->Add( wxXmlResource::Get()->LoadBitmap(wxT("plugin24")) );
		}

		for ( size_t i=0; i<files.GetCount(); i++ ) {
			ProjectPtr proj ( new Project() );
			if ( !proj->Load ( files.Item ( i ) ) ) {
				//corrupted xml file?
				wxLogMessage ( wxT ( "Failed to load template project: " ) + files.Item ( i ) + wxT ( " (corrupted XML?)" ) );
				continue;
			}
			list.push_back ( proj );

			// load template icon
			if ( imageMap ) {

				wxFileName fn( files.Item( i ) );
				wxString imageFileName(fn.GetPath( wxPATH_GET_SEPARATOR ) + wxT("icon.png") );
				if( wxFileExists( imageFileName )) {
					int img_id = (*lstImages)->Add( wxBitmap( fn.GetPath( wxPATH_GET_SEPARATOR ) + wxT("icon.png"), wxBITMAP_TYPE_PNG ) );;
					(*imageMap)[proj->GetName()] = img_id;
				}
			}
		}
	} else {
		//if we ended up here, it means the installation got screwed up since
		//there should be at least 8 project templates !
		//create 3 default empty projects
		ProjectPtr exeProj ( new Project() );
		ProjectPtr libProj ( new Project() );
		ProjectPtr dllProj ( new Project() );
		libProj->Create ( wxT ( "Static Library" ), wxEmptyString, tmplateDir, Project::STATIC_LIBRARY );
		dllProj->Create ( wxT ( "Dynamic Library" ), wxEmptyString, tmplateDir, Project::DYNAMIC_LIBRARY );
		exeProj->Create ( wxT ( "Executable" ), wxEmptyString, tmplateDir, Project::EXECUTABLE );
		list.push_back ( libProj );
		list.push_back ( dllProj );
		list.push_back ( exeProj );
	}
}

bool IsCppKeyword(const wxString& word)
{
	static std::set<wxString> words;

	if(words.empty()) {
		words.insert(wxT("auto"));
		words.insert(wxT("break"));
		words.insert(wxT("case"));
		words.insert(wxT("char"));
		words.insert(wxT("const"));
		words.insert(wxT("continue"));
		words.insert(wxT("default"));
		words.insert(wxT("define"));
		words.insert(wxT("defined"));
		words.insert(wxT("do"));
		words.insert(wxT("double"));
		words.insert(wxT("elif"));
		words.insert(wxT("else"));
		words.insert(wxT("endif"));
		words.insert(wxT("enum"));
		words.insert(wxT("error"));
		words.insert(wxT("extern"));
		words.insert(wxT("float"));
		words.insert(wxT("for"));
		words.insert(wxT("goto"));
		words.insert(wxT("if"));
		words.insert(wxT("ifdef"));
		words.insert(wxT("ifndef"));
		words.insert(wxT("include"));
		words.insert(wxT("int"));
		words.insert(wxT("line"));
		words.insert(wxT("long"));
		words.insert(wxT("bool"));
		words.insert(wxT("pragma"));
		words.insert(wxT("register"));
		words.insert(wxT("return"));
		words.insert(wxT("short"));
		words.insert(wxT("signed"));
		words.insert(wxT("sizeof"));
		words.insert(wxT("static"));
		words.insert(wxT("struct"));
		words.insert(wxT("switch"));
		words.insert(wxT("typedef"));
		words.insert(wxT("undef"));
		words.insert(wxT("union"));
		words.insert(wxT("unsigned"));
		words.insert(wxT("void"));
		words.insert(wxT("volatile"));
		words.insert(wxT("while"));
		words.insert(wxT("class"));
		words.insert(wxT("namespace"));
		words.insert(wxT("delete"));
		words.insert(wxT("friend"));
		words.insert(wxT("inline"));
		words.insert(wxT("new"));
		words.insert(wxT("operator"));
		words.insert(wxT("overload"));
		words.insert(wxT("protected"));
		words.insert(wxT("private"));
		words.insert(wxT("public"));
		words.insert(wxT("this"));
		words.insert(wxT("virtual"));
		words.insert(wxT("template"));
		words.insert(wxT("typename"));
		words.insert(wxT("dynamic_cast"));
		words.insert(wxT("static_cast"));
		words.insert(wxT("const_cast"));
		words.insert(wxT("reinterpret_cast"));
		words.insert(wxT("using"));
		words.insert(wxT("throw"));
		words.insert(wxT("catch"));
	}

	return words.find(word) != words.end();
}

bool ExtractFileFromZip(const wxString& zipPath, const wxString& filename, const wxString& targetDir, wxString &targetFileName) {
	wxZipEntry *       entry(NULL);
	wxFFileInputStream in(zipPath);
	wxZipInputStream   zip(in);

	wxString lowerCaseName(filename);
	lowerCaseName.MakeLower();

	entry = zip.GetNextEntry();
	while ( entry ) {
		wxString name = entry->GetName();
		name.MakeLower();
		name.Replace(wxT("\\"), wxT("/"));

		if (name == lowerCaseName) {
			name.Replace(wxT("/"), wxT("_"));
			targetFileName = wxString::Format(wxT("%s/%s"), targetDir.c_str(), name.c_str());
			wxFFileOutputStream out(targetFileName);
			zip.Read(out);
			out.Close();
			delete entry;
			return true;
		}

		delete entry;
		entry = zip.GetNextEntry();
	}
	return false;
}

void MSWSetNativeTheme(wxWindow* win, const wxString &theme)
{
#ifdef __WXMSW__
	SetWindowTheme((HWND)win->GetHWND(), theme.c_str(), NULL);
#endif
}

void StringManager::AddStrings(size_t size, const wxString* strings, const wxString& current, wxControlWithItems* control)
{
	m_size = size;
	m_unlocalisedStringArray = wxArrayString(size, strings);
	p_control = control;
	p_control->Clear();

	// Add each item to the control, localising as we go
	for (size_t n=0; n < size; ++n) {
		p_control->Append(wxGetTranslation(strings[n]));
	}
	
	// Select in the control the currently used string
	SetStringSelection(current);
}

wxString StringManager::GetStringSelection() const
{
	wxString selection;
	// Find which localised string was selected
	int sel = p_control->GetSelection();
	if (sel != wxNOT_FOUND) {
		selection = m_unlocalisedStringArray.Item(sel);
	}

	return selection;
}

void StringManager::SetStringSelection(const wxString& str, size_t dfault /*= 0*/)
{
	if (str.IsEmpty() || m_size == 0) {
		return;
	}
	int sel = m_unlocalisedStringArray.Index(str);
	if (sel != wxNOT_FOUND) {
		p_control->SetSelection(sel);
	} else {
		if (dfault < m_size) {
			p_control->SetSelection(dfault);
		} else {
			p_control->SetSelection(0);
		}
	} 
}

// Make absolute first, including abolishing any symlinks (Normalise only does MSW shortcuts)
// Then only 'make relative' if it's a subpath of reference_path (or reference_path itself)
bool MakeRelativeIfSensible(wxFileName& fn, const wxString& reference_path)
{
	if (reference_path.IsEmpty() || !fn.IsOk()) {
		return false;
	}

#if defined(__WXGTK__)
	// Normalize() doesn't account for symlinks in wxGTK
	wxStructStat statstruct;
	int error = wxLstat(fn.GetFullPath(), &statstruct);
	
	if (!error && S_ISLNK(statstruct.st_mode))	{	// If it's a symlink
		char buf[4096];
		int len = readlink(fn.GetFullPath().mb_str(wxConvUTF8), buf, WXSIZEOF(buf) - sizeof(char));
		if ( len != -1 ) {
			buf[len] = '\0'; // readlink() doesn't NULL-terminate the buffer
			fn.Assign(wxString(buf, wxConvUTF8, len));
		}
	}
#endif	

	fn.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_TILDE | wxPATH_NORM_SHORTCUT);
	
	// Now see if fn is in or under 'reference_path'
	wxString fnPath = fn.GetPath();
	if ((fnPath.Len() >= reference_path.Len()) && (fnPath.compare(0, reference_path.Len(), reference_path) == 0)) {
		fn.MakeRelativeTo(reference_path);
		return true;
	}

	return false;
}

wxString wxImplode(const wxArrayString &arr, const wxString &glue)
{
	wxString str, tmp;
	for(size_t i=0; i<arr.GetCount(); i++) {
		str << arr.Item(i) << glue;
	}
	
	if(str.EndsWith(glue, &tmp)){
		str = tmp;
	}
	return str;
}

////////////////////////////////////////
// BOM
////////////////////////////////////////

BOM::BOM(const char* buffer, size_t len)
{
	m_bom.AppendData(buffer, len);
}

BOM::BOM()
{
}

BOM::~BOM()
{
}

wxFontEncoding BOM::Encoding()
{
	wxFontEncoding encoding = Encoding((const char*)m_bom.GetData());
	if(encoding != wxFONTENCODING_SYSTEM) {
		switch(encoding) {
			
		case wxFONTENCODING_UTF32BE:
		case wxFONTENCODING_UTF32LE:
			m_bom.SetDataLen(4);
			break;
			
		case wxFONTENCODING_UTF8:
			m_bom.SetDataLen(3);
			break;
			
		case wxFONTENCODING_UTF16BE:
		case wxFONTENCODING_UTF16LE:
		default:
			m_bom.SetDataLen(2);
			break;
			
		}
	}
	return encoding;
}

wxFontEncoding BOM::Encoding(const char* buff)
{
	// Support for BOM:
	//----------------------------------
	//00 00 FE FF UTF-32, big-endian
	//FF FE 00 00 UTF-32, little-endian
	//FE FF       UTF-16, big-endian
	//FF FE       UTF-16, little-endian
	//EF BB BF    UTF-8
	//----------------------------------
	wxFontEncoding encoding = wxFONTENCODING_SYSTEM; /* -1 */

	static const char UTF32be[]= { 0x00, 0x00, 0xfe, 0xff};
	static const char UTF32le[]= { 0xff, 0xfe, 0x00, 0x00};
	static const char UTF16be[]= { 0xfe, 0xff            };
	static const char UTF16le[]= { 0xff, 0xfe            };
	static const char UTF8[]   = { 0xef, 0xbb, 0xbf      };
	
	if(memcmp(buff, UTF32be, sizeof(UTF32be)) == 0) {
		encoding = wxFONTENCODING_UTF32BE;
		
	} else if(memcmp(buff, UTF32le, sizeof(UTF32le)) == 0) {
		encoding = wxFONTENCODING_UTF32LE;
		
	} else if(memcmp(buff, UTF16be, sizeof(UTF16be)) == 0) {
		encoding = wxFONTENCODING_UTF16BE;
		
	} else if(memcmp(buff, UTF16le, sizeof(UTF16le)) == 0) {
		encoding = wxFONTENCODING_UTF16LE;
		
	} else if(memcmp(buff, UTF8, sizeof(UTF8)) == 0) {
		encoding = wxFONTENCODING_UTF8;
	}
	return encoding;
}

void BOM::SetData(const char* buffer, size_t len)
{
	m_bom = wxMemoryBuffer();
	m_bom.SetDataLen(0);
	m_bom.AppendData(buffer, len);
}

int BOM::Len() const
{
	return m_bom.GetDataLen();
}

void BOM::Clear()
{
	m_bom = wxMemoryBuffer();
	m_bom.SetDataLen(0);
}
