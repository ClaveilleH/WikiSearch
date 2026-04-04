grep "^INSERT INTO \`page\`" frwiki-latest-page.sql | sed 's/^INSERT INTO `page` VALUES //' | sed 's/),(/)\n(/g' | awk -F',' '
$2 == 0 {
  gsub(/\\'\''/, "'\'\''", $3)
  print "INSERT INTO page_simple VALUES " $1 "," $3 ");"
}
# '|    

INSERT INTO page_simple VALUES (16,'Arc_de_triomphe_de_l''Étoile');
