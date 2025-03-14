//
// XMLConfiguration.h
//
// Library: Util
// Package: Configuration
// Module:  XMLConfiguration
//
// Definition of the XMLConfiguration class.
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef DB_Util_XMLConfiguration_INCLUDED
#define DB_Util_XMLConfiguration_INCLUDED


#include "DBPoco/Util/Util.h"


#ifndef DB_POCO_UTIL_NO_XMLCONFIGURATION


#    include <istream>
#    include "DBPoco/DOM/AutoPtr.h"
#    include "DBPoco/DOM/DOMWriter.h"
#    include "DBPoco/DOM/Document.h"
#    include "DBPoco/SAX/InputSource.h"
#    include "DBPoco/Util/MapConfiguration.h"


namespace DBPoco
{
namespace Util
{


    class Util_API XMLConfiguration : public AbstractConfiguration
    /// This configuration class extracts configuration properties
    /// from an XML document. An XPath-like syntax for property
    /// names is supported to allow full access to the XML document.
    /// XML namespaces are not supported. The name of the root element
    /// of the XML document is not significant and ignored.
    /// Periods in tag names are not supported.
    ///
    /// Given the following XML document as an example:
    ///
    ///     <config>
    ///         <prop1>value1</prop1>
    ///         <prop2>value2</prop2>
    ///         <prop3>
    ///            <prop4 attr="value3"/>
    ///            <prop4 attr="value4"/>
    ///         </prop3>
    ///         <prop5 id="first">value5</prop5>
    ///         <prop5 id="second">value6</prop5>
    ///     </config>
    ///
    /// The following property names would be valid and would
    /// yield the shown values:
    ///
    ///     prop1                 -> value1
    ///     prop2                 -> value2
    ///     prop3.prop4           -> (empty string)
    ///     prop3.prop4[@attr]    -> value3
    ///     prop3.prop4[1][@attr] -> value4
    ///     prop5[0]              -> value5
    ///     prop5[1]              -> value6
    ///     prop5[@id=first]      -> value5
    ///     prop5[@id='second']   -> value6
    ///
    /// Enumerating attributes is not supported.
    /// Calling keys("prop3.prop4") will return an empty range.
    ///
    /// As a special feature, the delimiter character used to delimit
    /// property names can be changed to something other than period ('.') by
    /// passing the desired character to the constructor. This allows
    /// working with XML documents having element names with periods
    /// in them.
    {
    public:
        XMLConfiguration();
        /// Creates an empty XMLConfiguration with a "config" root element.

        XMLConfiguration(char delim);
        /// Creates an empty XMLConfiguration with a "config" root element,
        /// using the given delimiter char instead of the default '.'.

        XMLConfiguration(DBPoco::XML::InputSource * pInputSource);
        /// Creates an XMLConfiguration and loads the XML document from
        /// the given InputSource.

        XMLConfiguration(DBPoco::XML::InputSource * pInputSource, char delim);
        /// Creates an XMLConfiguration and loads the XML document from
        /// the given InputSource. Uses the given delimiter char instead
        /// of the default '.'.

        XMLConfiguration(std::istream & istr);
        /// Creates an XMLConfiguration and loads the XML document from
        /// the given stream.

        XMLConfiguration(std::istream & istr, char delim);
        /// Creates an XMLConfiguration and loads the XML document from
        /// the given stream. Uses the given delimiter char instead
        /// of the default '.'.

        XMLConfiguration(const std::string & path);
        /// Creates an XMLConfiguration and loads the XML document from
        /// the given path.

        XMLConfiguration(const std::string & path, char delim);
        /// Creates an XMLConfiguration and loads the XML document from
        /// the given path. Uses the given delimiter char instead
        /// of the default '.'.

        XMLConfiguration(const DBPoco::XML::Document * pDocument);
        /// Creates the XMLConfiguration using the given XML document.

        XMLConfiguration(const DBPoco::XML::Document * pDocument, char delim);
        /// Creates the XMLConfiguration using the given XML document.
        /// Uses the given delimiter char instead of the default '.'.

        XMLConfiguration(const DBPoco::XML::Node * pNode);
        /// Creates the XMLConfiguration using the given XML node.

        XMLConfiguration(const DBPoco::XML::Node * pNode, char delim);
        /// Creates the XMLConfiguration using the given XML node.
        /// Uses the given delimiter char instead of the default '.'.

        void load(DBPoco::XML::InputSource * pInputSource);
        /// Loads the XML document containing the configuration data
        /// from the given InputSource.

        void load(DBPoco::XML::InputSource * pInputSource, unsigned long namePoolSize);
        /// Loads the XML document containing the configuration data
        /// from the given InputSource. Uses the give namePoolSize (which
        /// should be a suitable prime like 251, 509, 1021, 4093) for the
        /// internal DOM Document's name pool.

        void load(std::istream & istr);
        /// Loads the XML document containing the configuration data
        /// from the given stream.

        void load(const std::string & path);
        /// Loads the XML document containing the configuration data
        /// from the given file.

        void load(const DBPoco::XML::Document * pDocument);
        /// Loads the XML document containing the configuration data
        /// from the given XML document.

        void load(const DBPoco::XML::Node * pNode);
        /// Loads the XML document containing the configuration data
        /// from the given XML node.

        void loadEmpty(const std::string & rootElementName);
        /// Loads an empty XML document containing only the
        /// root element with the given name.

        void save(const std::string & path) const;
        /// Writes the XML document containing the configuration data
        /// to the file given by path.

        void save(std::ostream & str) const;
        /// Writes the XML document containing the configuration data
        /// to the given stream.

        void save(DBPoco::XML::DOMWriter & writer, const std::string & path) const;
        /// Writes the XML document containing the configuration data
        /// to the file given by path, using the given DOMWriter.
        ///
        /// This can be used to use a DOMWriter with custom options.

        void save(DBPoco::XML::DOMWriter & writer, std::ostream & str) const;
        /// Writes the XML document containing the configuration data
        /// to the given stream.
        ///
        /// This can be used to use a DOMWriter with custom options.

    protected:
        bool getRaw(const std::string & key, std::string & value) const;
        void setRaw(const std::string & key, const std::string & value);
        void enumerate(const std::string & key, Keys & range) const;
        void removeRaw(const std::string & key);
        ~XMLConfiguration();

    private:
        const DBPoco::XML::Node * findNode(const std::string & key) const;
        DBPoco::XML::Node * findNode(const std::string & key);
        DBPoco::XML::Node * findNode(
            std::string::const_iterator & it, const std::string::const_iterator & end, DBPoco::XML::Node * pNode, bool create = false) const;
        static DBPoco::XML::Node * findElement(const std::string & name, DBPoco::XML::Node * pNode, bool create);
        static DBPoco::XML::Node * findElement(int index, DBPoco::XML::Node * pNode, bool create);
        static DBPoco::XML::Node * findElement(const std::string & attr, const std::string & value, DBPoco::XML::Node * pNode);
        static DBPoco::XML::Node * findAttribute(const std::string & name, DBPoco::XML::Node * pNode, bool create);

        DBPoco::XML::AutoPtr<DBPoco::XML::Node> _pRoot;
        DBPoco::XML::AutoPtr<DBPoco::XML::Document> _pDocument;
        char _delim;
    };


}
} // namespace DBPoco::Util


#endif // DB_POCO_UTIL_NO_XMLCONFIGURATION


#endif // DB_Util_XMLConfiguration_INCLUDED
