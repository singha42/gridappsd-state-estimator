#ifndef SPARQL_QUERIES_HPP
#define SPARQL_QUERIES_HPP

#include<string>

using std::string;

namespace sparql_queries {
	string sparq_conducting_equipment_vbase(string fdrid) {
		string sparq = "# Find the base voltage of each bus\n"
			"PREFIX r:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#>\n"
			"PREFIX c:  <http://iec.ch/TC57/2012/CIM-schema-cim17#>\n"
			"SELECT ?busid ?busname ?vbase WHERE {\n"
			"  # conducting equipment as an IdentifiedObject (PowerSystemResource)\n"
			"  ?ce c:IdentifiedObject.name ?cename.\n"
			"  # conducting equipment with a BaseVoltage\n"
			"  ?ce c:ConductingEquipment.BaseVoltage ?bvo.\n"
			"  ?bvo c:BaseVoltage.nominalVoltage ?vbase.\n"
			"  # terminals attached to conducting equipment\n"
			"  ?term c:Terminal.ConductingEquipment ?ce.\n"
			"  ?term c:Terminal.ConnectivityNode ?bus.\n"
			"  ?bus c:IdentifiedObject.name ?busname.\n"
			"  ?bus c:IdentifiedObject.mRID ?busid.\n"
			"  VALUES ?fdrid {\\\""+fdrid+"\\\"}\n"
			"  ?ce c:Equipment.EquipmentContainer ?fdr.\n"
			"  ?fdr c:IdentifiedObject.mRID ?fdrid.\n"
			"}\n"
			"GROUP BY ?busid ?busname ?vbase\n"
			"ORDER by ?busid\n";
		return sparq;
	}

	string sparq_transformer_end_vbase(string fdrid) {
		string sparq = "# Find the base voltage of each bus\n"
			"PREFIX r:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#>\n"
			"PREFIX c:  <http://iec.ch/TC57/2012/CIM-schema-cim17#>\n"
			"SELECT ?busid ?busname ?vbase ?cename WHERE {\n"
			"  # conducting equipment as an IdentifiedObject (PowerSystemResource)\n"
			"  ?ce c:IdentifiedObject.name ?cename.\n"
			"  # transformer ends associated with BaseVoltage object\n"
			"  ?te c:TransformerEnd.BaseVoltage ?bvo.\n"
			"  ?bvo c:BaseVoltage.nominalVoltage ?vbase.\n"
			"  # transformerends attached to terminals\n"
			"  ?te c:TransformerEnd.Terminal ?term.\n"
			"  ?term c:Terminal.ConnectivityNode ?bus.\n"
			"  ?bus c:IdentifiedObject.name ?busname.\n"
			"  ?bus c:IdentifiedObject.mRID ?busid.\n"
			"  VALUES ?fdrid {\\\""+fdrid+"\\\"}  # 13 bus\n"
			"  ?te c:PowerTransformerEnd.PowerTransformer ?ce.\n"
			"  ?ce c:Equipment.EquipmentContainer ?fdr.\n"
			"  ?fdr c:IdentifiedObject.mRID ?fdrid.\n"
			"}\n"
			"GROUP BY ?busid ?busname ?vbase ?cename\n"
			"ORDER by ?busid\n";
		return sparq;
	}

	string sparq_energy_consumer_pq(string fdrid) {
		string sparq = "# Find the base voltage of each bus\n"
			"PREFIX r:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#>\n"
			"PREFIX c:  <http://iec.ch/TC57/2012/CIM-schema-cim17#>\n"
			"SELECT ?loadname ?busname ?conn ?phase ?pfixed ?qfixed "
				"?pfixedphase ?qfixedphase WHERE {\n"
			"  # conducting equipment as an IdentifiedObject (PowerSystemResource)\n"
			"  ?econsumer c:IdentifiedObject.name ?loadname.\n"
			"  # terminals attached to conducting equipment\n"
			"  ?term c:Terminal.ConductingEquipment ?econsumer.\n"
			"  ?term c:Terminal.ConnectivityNode ?bus.\n"
			"  ?bus c:IdentifiedObject.name ?busname.\n"
			"  ?bus c:IdentifiedObject.mRID ?busid.\n"
			"  # p and q\n"
			"  ?econsumer c:EnergyConsumer.pfixed ?pfixed.\n"
			"  ?econsumer c:EnergyConsumer.qfixed ?qfixed.  \n"
			"  # connection type\n"
			"  ?econsumer c:EnergyConsumer.phaseConnection ?connraw.\n"
			"  bind(strafter(str(?connraw),\\\"PhaseShuntConnectionKind.\\\") as ?conn)\n"
			"  # phases of the consumer\n"
			"  OPTIONAL {\n"
			"    ?ecp c:EnergyConsumerPhase.EnergyConsumer ?econsumer.\n"
			"    ?ecp c:EnergyConsumerPhase.phase ?phsraw.\n"
			"    bind(strafter(str(?phsraw),\\\"SinglePhaseKind.\\\") as ?phase)\n"
			"    ?ecp c:EnergyConsumerPhase.pfixed ?pfixedphase.\n"
			"    ?ecp c:EnergyConsumerPhase.qfixed ?qfixedphase.\n"
			"  }\n"
			"  VALUES ?fdrid {\\\""+fdrid+"\\\"}  # 13 bus\n"
			"  ?econsumer c:Equipment.EquipmentContainer ?fdr.\n"
			"  ?fdr c:IdentifiedObject.mRID ?fdrid.\n"
			"}\n"
			"GROUP BY ?loadname ?busname ?conn ?phase ?pfixed ?qfixed ?pfixedphase ?qfixedphase\n"
			"ORDER by ?loadname\n";
		return sparq;
	}

	string sparq_ratio_tap_changer_nodes(string fdrid) { 
		string sparq = "# Find the nodes of each regulator\n"
			"PREFIX r:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#>\n"
			"PREFIX c:  <http://iec.ch/TC57/2012/CIM-schema-cim17#>\n"
			"SELECT ?rtcname ?xtname ?primbus ?primphs ?regbus ?regphs WHERE {\n"
			"  # \n"
			"  ?rtc c:IdentifiedObject.name ?rtcname.\n"
			"  ?rtc c:RatioTapChanger.TransformerEnd ?rte.\n"
			"  ?rte c:TransformerTankEnd.TransformerTank ?xt.\n"
			"  ?rte c:TransformerEnd.Terminal ?rterm.\n"
			"  ?rte c:TransformerTankEnd.phases ?rphsraw.\n"
			"  bind(strafter(str(?rphsraw),\\\"PhaseCode.\\\") as ?regphs)\n"
			"  ?rterm c:Terminal.ConnectivityNode ?rcn.\n"
			"  ?rcn c:IdentifiedObject.name ?regbus.\n"
			"  ?te c:TransformerTankEnd.TransformerTank ?xt.\n"
			"  ?te c:TransformerTankEnd.phases ?phsraw.\n"
			"  bind(strafter(str(?phsraw),\\\"PhaseCode.\\\") as ?primphs)\n"
			"  ?xt c:IdentifiedObject.name ?xtname.\n"
			"  ?te c:TransformerEnd.Terminal ?term.\n"
			"  ?term c:Terminal.ConnectivityNode ?cn.\n"
			"  ?cn c:IdentifiedObject.name ?primbus.\n"
			"  ?xt c:Equipment.EquipmentContainer ?fdr.\n"
			"  ?fdr c:IdentifiedObject.mRID ?fdrid.\n"
			"  VALUES ?fdrid {\\\""+fdrid+"\\\"}\n"
			"  FILTER ( ?primbus NOT IN ( ?regbus ) )\n"
			"}\n"
			"GROUP BY ?rtcname ?xtname ?primbus ?primphs ?regbus ?regphs\n"
			"ORDER by ?rtcname\n";
		return sparq;
	}
}

#endif
