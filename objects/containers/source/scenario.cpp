/*! 
* \file scenario.cpp
* \ingroup CIAM
* \brief Scenario class source file.
* \author Sonny Kim
* \date $Date$
* \version $Revision$
*/				

#include "util/base/include/definitions.h"
#include <string>
#include <iostream>
#include <fstream>
#include <cassert>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMNodeList.hpp>

// User headers
#include "containers/include/scenario.h"
#include "util/base/include/model_time.h"
#include "marketplace/include/marketplace.h"
#include "containers/include/world.h"
#include "util/base/include/xml_helper.h"
#include "util/base/include/configuration.h"
#include "util/logger/include/logger_factory.h"
#include "util/logger/include/logger.h"
#include "util/curves/include/curve.h"
#include "solution/solvers/include/solver.h"
#include "solution/solvers/include/bisection_nr_solver.h"

using namespace std;
using namespace xercesc;

void writeClimatData(void); // function to write data for climat
#if(__HAVE_FORTRAN__)
extern "C" { void _stdcall CLIMAT(void); };
#endif

extern time_t ltime;
extern ofstream logfile;

const string Scenario::XML_NAME = "scenario";

//! Default construtor
/*! \todo Implement a factory method which chooses solvers to abstract this further. -JPL
*/
Scenario::Scenario() {
    runCompleted = false;
    marketplace.reset( new Marketplace() );

    // Create the solver and initialize with a pointer to the Marketplace.
    solver.reset( new BisectionNRSolver( marketplace.get() ) );
}

//! Destructor
Scenario::~Scenario() {
    clear();
}

//! Perform memory deallocation.
void Scenario::clear() {
}

//! Return a reference to the modeltime->
const Modeltime* Scenario::getModeltime() const {
    return modeltime.get();
}

//! Return a constant reference to the goods and services marketplace.
const Marketplace* Scenario::getMarketplace() const {
    return marketplace.get();
}

//! Return a mutable reference to the goods and services marketplace.
Marketplace* Scenario::getMarketplace() {
    return marketplace.get();
}

//! Return a constant reference to the world object.
const World* Scenario::getWorld() const {
    return world.get();
}

//! Return a mutable reference to the world object.
World* Scenario::getWorld() {
    return world.get();
}

//! Set data members from XML input.
void Scenario::XMLParse( const DOMNode* node ){

    DOMNode* curr = 0;
    DOMNodeList* nodeList;
    string nodeName;

    // assume we were passed a valid node.
    assert( node );

    // set the scenario name
    name = XMLHelper<string>::getAttrString( node, "name" );

    // get the children of the node.
    nodeList = node->getChildNodes();

    // loop through the children
    for ( int i = 0; i < static_cast<int>( nodeList->getLength() ); i++ ){
        curr = nodeList->item( i );
        nodeName = XMLHelper<string>::safeTranscode( curr->getNodeName() );

        if( nodeName == "#text" ) {
            continue;
        }

        else if ( nodeName == "summary" ){
            scenarioSummary = XMLHelper<string>::getValueString( curr );
        }

		else if ( nodeName == Modeltime::getXMLNameStatic() ){
            if( !modeltime.get() ) {
                modeltime.reset( new Modeltime() );
                modeltime->XMLParse( curr );
                modeltime->set(); // This call cannot be delayed until completeInit() because it is needed first. 
            }
            else {
                cout << "Modeltime information cannot be modified in a scenario add-on." << endl;
            }
        }
		else if ( nodeName == World::getXMLNameStatic() ){
            if( !world.get() ) {
                world.reset( new World() );
            }
            world->XMLParse( curr );
        }
        else {
            cout << "Unrecognized text string: " << nodeName << " found while parsing scenario." << endl;
        }
    }
}

//! Sets the name of the scenario. 
void Scenario::setName(string newName) {

    // Used to override the read-in scenario name.
    name = newName;
}

//! Finish all initializations needed before the model can run.
void Scenario::completeInit() {

    // Complete the init of the world object.
    assert( world.get() );
    world->completeInit();
}

//! Write object to xml output stream.
void Scenario::toInputXML( ostream& out, Tabs* tabs ) const {
    
    // write heading for XML input file
    bool header = true;
    if (header) {
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
        out << "<!-- edited with XMLSPY v5 rel. 2 U (http://www.xmlspy.com)";
        out << "by Son H. Kim (PNNL) -->" << endl;
        out << "<!--XML file generated by XMLSPY v5 rel. 2 U (http://www.xmlspy.com)-->" << endl;
    }

    string dateString = util::XMLCreateDate( ltime );
    out << "<" << XML_NAME << " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"";
    out << " xsi:noNamespaceSchemaLocation=\"C:\\PNNL\\CIAM\\CVS\\CIAM\\Ciam.xsd\"";
    out << " name=\"" << name << "\" date=\"" << dateString << "\">" << endl;
    // increase the indent.
    tabs->increaseIndent();

    // summary notes on scenario
    tabs->writeTabs( out );
    out << "<summary>\"SRES B2 Scenario is used for this Reference Scenario\"</summary>" << endl;

    // write the xml for the class members.
    modeltime->toInputXML( out, tabs );
    world->toInputXML( out, tabs );
    // finished writing xml for the class members.

	XMLWriteClosingTag( XML_NAME, out, tabs );
}

//! Write out object to output stream for debugging.
void Scenario::toDebugXMLOpen( ostream& out, Tabs* tabs ) const {
    string dateString = util::XMLCreateDate( ltime );
    out << "<" << XML_NAME << " name=\"" << name << "\" date=\"" << dateString << "\">" << endl;

    tabs->increaseIndent();
    XMLWriteElement( "Debugging output", "summary", out, tabs );
}

//! Write out close scenario tag to output stream for debugging.
void Scenario::toDebugXMLClose( ostream& out, Tabs* tabs ) const {
    XMLWriteClosingTag( XML_NAME, out, tabs );
}

//! Return scenario name.
string Scenario::getName() const {
    return name; 
}

//! Run the scenario
void Scenario::run( string filenameEnding ){

    Configuration* conf = Configuration::getInstance();
    ofstream xmlDebugStream;
    openDebugXMLFile( xmlDebugStream, filenameEnding );

    // Print the sector dependencies. This may need a better spot 
    // and name as it now prints sector ordering as well. 
    if( conf->getBool( "PrintSectorDependencies", false ) ){
        printSectorDependencies();
    }

    Tabs tabs;
    marketplace->initPrices(); // initialize prices
    toDebugXMLOpen( xmlDebugStream, &tabs );

    // Loop over time steps and operate model
    for ( int per = 0; per < modeltime->getmaxper(); per++ ) {	

        // Write out some info.
        cout << endl << "Period " << per <<": "<< modeltime->getper_to_yr( per ) << endl;
        logfile << "Period:  " << per << "  Year:  " << modeltime->getper_to_yr(per) << endl;

        // Run the iteration of the model.
        marketplace->nullDemands( per ); // initialize market demand to null
        marketplace->nullSupplies( per ); // initialize market supply to null
        marketplace->storeto_last( per ); // save last period's info to stored variables
        marketplace->init_to_last( per ); // initialize to last period's info
        world->initCalc( per ); // call to initialize anything that won't change during calc
        world->calc( per ); // call to calculate initial supply and demand
        solve( per ); // solution uses Bisect and NR routine to clear markets
        world->updateSummary( per ); // call to update summaries for reporting
        world->emiss_ind( per ); // call to calculate global emissions

        // Write out the results for debugging.
        world->toDebugXML( per, xmlDebugStream, &tabs );

        if( conf->getBool( "PrintDependencyGraphs" ) ) {
            printGraphs( per ); // Print out dependency graphs.
        }
    }

    // Denote the run has been performed. 
    runCompleted = true;

    toDebugXMLClose( xmlDebugStream, &tabs ); // Close the xml debugging tag.

    // calling fortran subroutine climat/magicc
    world->calculateEmissionsTotals();
    writeClimatData(); // writes the input text file

#if(__HAVE_FORTRAN__)
    cout << endl << "Calling CLIMAT() "<< endl;
    CLIMAT();
    cout << "Finished with CLIMAT()" << endl;
#endif
    xmlDebugStream.close();
}

/*! \brief A function which print dependency graphs showing fuel usage by sector.
*
* This function creates a filename and stream for printing the graph data in the dot graphing language.
* The filename is created from the dependencyGraphName configuration attribute concatenated with the period.
* The function then calls the World::printDependencyGraphs function to perform the printing.
* Once the data is printed, dot must be called to create the actual graph as follows:
* dot -Tpng depGraphs_8.dot -o graphs.png
* where depGraphs_8.dot is the file created by this function and graphs.png is the file you want to create.
* The output format can be changed, see the dot documentation for further information.
*
* \param period The period to print graphs for.
* \return void
*/
void Scenario::printGraphs( const int period ) const {

    Configuration* conf = Configuration::getInstance();
    string fileName;
    ofstream graphStream;
    stringstream fileNameBuffer;

    // Create the filename.
    fileNameBuffer << conf->getFile( "dependencyGraphName", "graph" ) << "_" << period << ".dot";
    fileNameBuffer >> fileName;

    graphStream.open( fileName.c_str() );
    util::checkIsOpen( graphStream, fileName );

    world->printGraphs( graphStream, period );

    graphStream.close();
}

/*! \brief A function to print a csv file including the list of all regions and their sector dependencies.
* 
* \author Josh Lurz
*/
void Scenario::printSectorDependencies() const {
    Logger* logger = LoggerFactory::getLogger( "SectorDependencies.csv" );
    world->printSectorDependencies( logger );
}

/*! \brief A function to generate a series of ghg emissions quantity curves based on an already performed model run.
* \details This function used the information stored in it to create a series of curves, one for each region,
* with each datapoint containing a time period and an amount of gas emissions.
* \note The user is responsible for deallocating the memory in the returned Curves.
* \author Josh Lurz
* \param The name of the ghg to create a set of curves for.
* \return A vector of Curve objects representing ghg emissions quantity by time period by region.
*/
const map<const string, const Curve*> Scenario::getEmissionsQuantityCurves( const string& ghgName ) const {
    /*! \pre The run has been completed. */
    return world->getEmissionsQuantityCurves( ghgName );
}

/*! \brief A function to generate a series of ghg emissions price curves based on an already performed model run.
* \details This function used the information stored in it to create a series of curves, one for each period,
* with each datapoint containing a time period and the price gas emissions. 
* \note The user is responsible for deallocating the memory in the returned Curves.
* \author Josh Lurz
* \param The name of the ghg to create a set of curves for.
* \return A vector of Curve objects representing the price of ghg emissions by time period by Region. 
*/
const map<const string,const Curve*> Scenario::getEmissionsPriceCurves( const string& ghgName ) const {
    /*! \pre The run has been completed. */
    return world->getEmissionsPriceCurves( ghgName );
}

/*! \brief Solve the marketplace using the Solver for a given period. 
*
* The solve method calls the solve method of the instance of the Solver object 
* that was created in the constructor. This method then checks for any errors that occurred while solving
* and reports the errors if it is the last period. 
*
* \param period Period of the model to solve.
* \todo Fix the return codes. 
*/

void Scenario::solve( const int period ){
    // Solve the marketplace. If the retcode is zero, add it to the unsolved periods. 
    if( !solver->solve( period ) ) {
        unsolvedPeriods.push_back( period );
    }

    // If it was the last period print the ones that did not solve.
    if( modeltime->getmaxper() - 1 == period  ){
        if( static_cast<int>( unsolvedPeriods.size() ) == 0 ) {
            cout << "All model periods solved correctly." << endl;
        }
        else {
            cout << "The following model periods did not solve: ";
            for( vector<int>::const_iterator i = unsolvedPeriods.begin(); i != unsolvedPeriods.end(); i++ ) {
                cout << *i << ", ";
            }
            cout << endl;
        }
    }
}

//! Open the debugging XML file with the correct name and check for any errors.
void Scenario::openDebugXMLFile( ofstream& xmlDebugStream, const string& fileNameEnding ){
    // Need to insert the filename ending before the file type.
    const Configuration* conf = Configuration::getInstance();
    string debugFileName = conf->getFile( "xmlDebugFileName", "debug.xml" );
    size_t dotPos = debugFileName.find_last_of( "." );
    debugFileName = debugFileName.insert( dotPos, fileNameEnding );
    cout << "Debugging information for this run in: " << debugFileName << endl;
    xmlDebugStream.open( debugFileName.c_str(), ios::out );
    util::checkIsOpen( xmlDebugStream, debugFileName );
}

