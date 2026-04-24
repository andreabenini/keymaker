COMMENT=$1

echo "git add . && git commit -m \"$COMMENT\""
git add . && git commit -m "$COMMENT"
